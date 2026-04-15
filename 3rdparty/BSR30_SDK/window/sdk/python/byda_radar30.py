"""
BYDA Radar SDK - BSR30 Python Wrapper
======================================
Python ctypes wrapper for BSR30_SDK (BSR30_SDK.dll / libBSR30_SDK.so).

Usage:
    from byda_radar30 import Bsr30Radar

    def on_frame(frame_data):
        print(f"seq={frame_data['sequence']}, active={len(frame_data['tracks'])}")

    radar = Bsr30Radar()
    radar.set_frame_callback(on_frame)
    radar.connect("192.168.172.128", 8088, 9001)
    radar.start()
"""

import ctypes
import ctypes.util
import os
import sys
from dataclasses import dataclass
from typing import List, Optional, Callable

# ------------------------------------------------------------------ #
# C-compatible structures (must match Bsr30Sdk.h)                     #
# ------------------------------------------------------------------ #

BSR30_TRACK_COUNT = 1024


class _Bsr30Track(ctypes.Structure):
    _pack_ = 1
    _fields_ = [
        ("id",            ctypes.c_uint8),
        ("_reserved0",    ctypes.c_uint8),
        ("pw",            ctypes.c_uint16),
        ("spFlag",        ctypes.c_uint32),
        ("_reserved1",    ctypes.c_float),
        ("initPosVY_kph", ctypes.c_float),
        ("xPos_m",        ctypes.c_float),
        ("yPos_m",        ctypes.c_float),
        ("xVel_kph",      ctypes.c_float),
        ("yVel_kph",      ctypes.c_float),
        ("laneNum",       ctypes.c_int8),
        ("vehicleType",   ctypes.c_uint8),
        ("_reserved2",    ctypes.c_uint8),
        ("initLaneNum",   ctypes.c_int8),
        ("_padding",      ctypes.c_uint8 * 4),
    ]


class _Bsr30Frame(ctypes.Structure):
    _fields_ = [
        ("sequence",  ctypes.c_uint16),
        ("timestamp", ctypes.c_uint32),
        ("tracks",    _Bsr30Track * BSR30_TRACK_COUNT),
    ]


class _Bsr30SdkVersion(ctypes.Structure):
    _fields_ = [
        ("major",        ctypes.c_int),
        ("minor",        ctypes.c_int),
        ("patch",        ctypes.c_int),
        ("name",         ctypes.c_char_p),
        ("manufacturer", ctypes.c_char_p),
    ]


# Callback: void (*bsr30_frame_cb)(const bsr30_frame_t* frame)
_FRAME_CB = ctypes.CFUNCTYPE(None, ctypes.POINTER(_Bsr30Frame))

# ------------------------------------------------------------------ #
# Python dataclasses                                                   #
# ------------------------------------------------------------------ #

@dataclass
class Bsr30Track:
    id: int
    pw: int
    xPos_m: float
    yPos_m: float
    xVel_kph: float
    yVel_kph: float
    laneNum: int
    vehicleType: int


# ------------------------------------------------------------------ #
# Library loader                                                       #
# ------------------------------------------------------------------ #

_cached_lib: Optional[ctypes.CDLL] = None  # 싱글톤 DLL — 절대 언로드 안 함


def _load_library() -> ctypes.CDLL:
    """DLL 싱글톤 로드. 샘플 코드와 동일하게 프로세스 수명 동안 유지."""
    global _cached_lib
    if _cached_lib is not None:
        return _cached_lib

    script_dir = os.path.dirname(os.path.abspath(__file__))
    bin_dir = os.path.normpath(os.path.join(script_dir, '..', 'bin'))

    # uv.dll must be loadable (same dir as BSR30_SDK.dll)
    if sys.platform == "win32":
        os.environ['PATH'] = bin_dir + os.pathsep + os.environ.get('PATH', '')
        try:
            ctypes.CDLL(os.path.join(bin_dir, "uv.dll"))
        except OSError:
            pass

    names = []
    if sys.platform == "win32":
        names = [
            os.path.join(bin_dir, "BSR30_SDK.dll"),
            "BSR30_SDK.dll",
        ]
    else:
        names = [
            os.path.join(bin_dir, "libBSR30_SDK.so"),
            "libBSR30_SDK.so",
        ]

    errors = []
    for name in names:
        try:
            lib = ctypes.CDLL(name)
            _cached_lib = lib
            return lib
        except OSError as e:
            errors.append(f"  {name}: {e}")
            continue

    raise OSError(
        "Cannot find BSR30_SDK shared library.\n"
        f"bin_dir: {bin_dir}\n" +
        "\n".join(errors)
    )


def _setup_prototypes(lib: ctypes.CDLL):
    lib.bsr30_sdk_get_version.restype = None
    lib.bsr30_sdk_get_version.argtypes = [ctypes.POINTER(_Bsr30SdkVersion)]

    lib.bsr30_connect.restype = ctypes.c_bool
    lib.bsr30_connect.argtypes = [ctypes.c_char_p, ctypes.c_int, ctypes.c_int]

    lib.bsr30_disconnect.restype = None
    lib.bsr30_disconnect.argtypes = []

    lib.bsr30_radar_start.restype = ctypes.c_bool
    lib.bsr30_radar_start.argtypes = []

    lib.bsr30_radar_stop.restype = ctypes.c_bool
    lib.bsr30_radar_stop.argtypes = []

    lib.bsr30_set_radar_frame_callback.restype = None
    lib.bsr30_set_radar_frame_callback.argtypes = [_FRAME_CB]


# ------------------------------------------------------------------ #
# Pythonic wrapper  (싱글톤 — 샘플 코드와 동일하게 프로세스당 1개)      #
# ------------------------------------------------------------------ #

_instance: Optional['Bsr30Radar'] = None


class Bsr30Radar:
    """BSR30 Radar SDK wrapper (싱글톤)."""

    def __new__(cls):
        global _instance
        if _instance is not None:
            return _instance
        obj = super().__new__(cls)
        obj._initialized = False
        _instance = obj
        return obj

    def __init__(self):
        if self._initialized:
            return
        self._lib = _load_library()
        _setup_prototypes(self._lib)
        self._connected = False
        self._streaming = False
        self._user_callback: Optional[Callable] = None
        # prevent GC of the C callback
        self._c_callback: Optional[_FRAME_CB] = None
        self._initialized = True

    def get_version(self) -> str:
        ver = _Bsr30SdkVersion()
        self._lib.bsr30_sdk_get_version(ctypes.byref(ver))
        name = ver.name.decode('utf-8') if ver.name else "BSR30"
        return f"{name} v{ver.major}.{ver.minor}.{ver.patch}"

    def connect(self, ip: str, tcp_port: int = 8088, udp_port: int = 9001) -> bool:
        ok = self._lib.bsr30_connect(ip.encode('utf-8'), tcp_port, udp_port)
        self._connected = bool(ok)
        return self._connected

    def disconnect(self):
        if self._streaming:
            self.stop()
        if self._connected:
            self._lib.bsr30_disconnect()
            self._connected = False

    def force_disconnect(self):
        """DLL 자동 초기화 정리용 — 상태 무시하고 disconnect 호출."""
        try:
            self._lib.bsr30_radar_stop()
        except OSError:
            pass
        try:
            self._lib.bsr30_disconnect()
        except OSError:
            pass
        self._connected = False
        self._streaming = False

    def start(self) -> bool:
        if not self._connected:
            return False
        ok = self._lib.bsr30_radar_start()
        self._streaming = bool(ok)
        return self._streaming

    def stop(self) -> bool:
        if not self._streaming:
            return True
        ok = self._lib.bsr30_radar_stop()
        self._streaming = False
        return bool(ok)

    def set_frame_callback(self, callback: Callable):
        """Set Python callback: callback(frame_dict).
        frame_dict = {'sequence': int, 'timestamp': int, 'tracks': [Bsr30Track, ...]}
        Only active tracks (pw > 0) are included.
        """
        self._user_callback = callback

        def _c_cb(frame_ptr):
            if not self._user_callback:
                return
            frame = frame_ptr.contents
            tracks = []
            for i in range(BSR30_TRACK_COUNT):
                t = frame.tracks[i]
                if t.pw == 0:
                    continue
                tracks.append(Bsr30Track(
                    id=t.id, pw=t.pw,
                    xPos_m=t.xPos_m, yPos_m=t.yPos_m,
                    xVel_kph=t.xVel_kph, yVel_kph=t.yVel_kph,
                    laneNum=t.laneNum, vehicleType=t.vehicleType,
                ))
            self._user_callback({
                'sequence': frame.sequence,
                'timestamp': frame.timestamp,
                'tracks': tracks,
            })

        self._c_callback = _FRAME_CB(_c_cb)
        self._lib.bsr30_set_radar_frame_callback(self._c_callback)

    def close(self):
        """연결 해제. DLL + 콜백은 유지 (싱글톤)."""
        self.disconnect()
        # 콜백은 해제하지 않음 — DLL이 댕글링 포인터를 참조할 수 있음
        self._user_callback = None

    def __enter__(self):
        return self

    def __exit__(self, *args):
        self.close()

    def __del__(self):
        # 싱글톤 — 프로세스 종료 시에만 호출됨, 정리 불필요
        pass
