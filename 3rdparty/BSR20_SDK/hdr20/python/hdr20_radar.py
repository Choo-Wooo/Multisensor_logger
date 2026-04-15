"""
HDR20 (4D Radar) SDK - Python Wrapper
======================================
Python ctypes wrapper for the BSR20_SDK shared library,
using the 4D radar frame callback API.

Usage:
    from hdr20_radar import Hdr20Radar

    def on_frame(frame_info):
        print(f"Frame {frame_info.frame_num}: {frame_info.dyn_high_count} dyn_high, "
              f"{frame_info.track_count} tracks")

    with Hdr20Radar("192.168.172.128", 7) as radar:
        radar.set_frame_callback(on_frame)
        radar.connect()
        radar.receive_loop()
"""

import ctypes
import ctypes.util
import os
import sys
from dataclasses import dataclass, field
from typing import Callable, List, Optional

# ------------------------------------------------------------------ #
# C-compatible structures                                              #
# ------------------------------------------------------------------ #

class _BydaVdsConfig(ctypes.Structure):
    _pack_ = 4
    _fields_ = [
        ("det_line_up",   ctypes.c_float),
        ("det_line_dn",   ctypes.c_float),
        ("up_band_st",    ctypes.c_float),
        ("up_band_width", ctypes.c_float),
        ("dn_band_st",    ctypes.c_float),
        ("dn_band_width", ctypes.c_float),
        ("car2car_time",  ctypes.c_float),
    ]

# ------------------------------------------------------------------ #
# Python dataclasses                                                   #
# ------------------------------------------------------------------ #

@dataclass
class DynHighPoint:
    x: float
    y: float
    z: float
    vel: float
    snr: int

@dataclass
class TrackPoint:
    x: float
    y: float
    z: float

@dataclass
class FrameInfo:
    frame_num: int
    dyn_high_count: int
    dyn_low_count: int
    mm_long_high_count: int
    mm_long_low_count: int
    mm_short_high_count: int
    mm_short_low_count: int
    track_count: int
    dyn_high_points: List[DynHighPoint] = field(default_factory=list)
    track_points: List[TrackPoint] = field(default_factory=list)

# ------------------------------------------------------------------ #
# Error codes                                                          #
# ------------------------------------------------------------------ #

BYDA_OK             =  0
BYDA_ERR_NULL       = -1
BYDA_ERR_CONNECT    = -2
BYDA_ERR_RECV       = -3
BYDA_ERR_NO_DATA    = -4
BYDA_ERR_INDEX      = -5
BYDA_ERR_PARAM_BUSY = -6
BYDA_ERR_NOT_CONN   = -7

class BydaError(Exception):
    _MESSAGES = {
        BYDA_ERR_NULL:       "Null handle",
        BYDA_ERR_CONNECT:    "Connection failed",
        BYDA_ERR_RECV:       "Receive failed",
        BYDA_ERR_NO_DATA:    "No data available",
        BYDA_ERR_INDEX:      "Invalid index",
        BYDA_ERR_PARAM_BUSY: "Parameter load busy",
        BYDA_ERR_NOT_CONN:   "Not connected",
    }
    def __init__(self, code: int):
        self.code = code
        msg = self._MESSAGES.get(code, f"Unknown error ({code})")
        super().__init__(msg)

# ------------------------------------------------------------------ #
# Library loader                                                       #
# ------------------------------------------------------------------ #

def _load_library() -> ctypes.CDLL:
    names = []
    script_dir = os.path.dirname(os.path.abspath(__file__))
    sample_dir = os.path.normpath(os.path.join(script_dir, "..", ".."))

    if sys.platform == "win32":
        # Look in sample/window/sdk/32bit/bin/ and 64bit/bin/
        import struct
        bits = struct.calcsize("P") * 8
        bit_dir = "32bit" if bits == 32 else "64bit"
        sdk_bin = os.path.join(sample_dir, "window", "sdk", bit_dir, "bin")
        if os.path.isdir(sdk_bin):
            os.environ["PATH"] = sdk_bin + os.pathsep + os.environ.get("PATH", "")
            if hasattr(os, "add_dll_directory"):
                os.add_dll_directory(sdk_bin)

        names = [
            os.path.join(sdk_bin, "BSR20_SDK.dll"),
            os.path.join(script_dir, "BSR20_SDK.dll"),
            "BSR20_SDK.dll",
        ]
    else:
        import struct
        bits = struct.calcsize("P") * 8
        if bits != 64:
            raise OSError(
                "BSR20_SDK Python sample requires 64-bit Python. "
                "32-bit Python is not supported on Linux."
            )
        sdk_lib = os.path.join(sample_dir, "linux", "sdk", "64bit", "lib")
        names = [
            os.path.join(sdk_lib, "libBSR20_SDK.so"),
            os.path.join(script_dir, "libBSR20_SDK.so"),
            "libBSR20_SDK.so",
        ]

    for name in names:
        try:
            return ctypes.CDLL(name)
        except OSError:
            continue

    path = ctypes.util.find_library("BSR20_SDK")
    if path:
        return ctypes.CDLL(path)

    raise OSError(
        "Cannot find BSR20_SDK shared library. "
        "Place the DLL/SO in the sdk directory or set PATH/LD_LIBRARY_PATH."
    )

# Callback types
_RAW_CB_TYPE = ctypes.CFUNCTYPE(None, ctypes.POINTER(ctypes.c_uint8), ctypes.c_int, ctypes.c_void_p)
_FRAME_CB_TYPE = ctypes.CFUNCTYPE(None, ctypes.c_void_p, ctypes.c_void_p)

def _setup_prototypes(lib: ctypes.CDLL):
    c_handle = ctypes.c_void_p

    # Lifecycle
    lib.byda_create.restype = c_handle
    lib.byda_create.argtypes = []
    lib.byda_destroy.restype = None
    lib.byda_destroy.argtypes = [c_handle]

    # Configuration
    lib.byda_set_address.restype = ctypes.c_int
    lib.byda_set_address.argtypes = [c_handle, ctypes.c_char_p, ctypes.c_int]
    lib.byda_get_sdk_version.restype = ctypes.c_float
    lib.byda_get_sdk_version.argtypes = []
    lib.byda_get_comm_version.restype = ctypes.c_int
    lib.byda_get_comm_version.argtypes = [c_handle]

    # Connection
    lib.byda_connect.restype = ctypes.c_int
    lib.byda_connect.argtypes = [c_handle]
    lib.byda_disconnect.restype = None
    lib.byda_disconnect.argtypes = [c_handle]

    # Data loop
    lib.byda_receive.restype = ctypes.c_int
    lib.byda_receive.argtypes = [c_handle]

    # Raw callback
    lib.byda_set_raw_callback.restype = ctypes.c_int
    lib.byda_set_raw_callback.argtypes = [c_handle, _RAW_CB_TYPE, ctypes.c_void_p]

    # Frame callback
    lib.byda_set_frame_callback.restype = ctypes.c_int
    lib.byda_set_frame_callback.argtypes = [c_handle, _FRAME_CB_TYPE, ctypes.c_void_p]

    # Frame accessors
    lib.byda_frame_get_num.restype = ctypes.c_uint32
    lib.byda_frame_get_num.argtypes = [ctypes.c_void_p]

    lib.byda_frame_get_dyn_high_count.restype = ctypes.c_int
    lib.byda_frame_get_dyn_high_count.argtypes = [ctypes.c_void_p]

    lib.byda_frame_get_dyn_low_count.restype = ctypes.c_int
    lib.byda_frame_get_dyn_low_count.argtypes = [ctypes.c_void_p]

    lib.byda_frame_get_mm_long_high_count.restype = ctypes.c_int
    lib.byda_frame_get_mm_long_high_count.argtypes = [ctypes.c_void_p]

    lib.byda_frame_get_mm_long_low_count.restype = ctypes.c_int
    lib.byda_frame_get_mm_long_low_count.argtypes = [ctypes.c_void_p]

    lib.byda_frame_get_mm_short_high_count.restype = ctypes.c_int
    lib.byda_frame_get_mm_short_high_count.argtypes = [ctypes.c_void_p]

    lib.byda_frame_get_mm_short_low_count.restype = ctypes.c_int
    lib.byda_frame_get_mm_short_low_count.argtypes = [ctypes.c_void_p]

    lib.byda_frame_get_track_count.restype = ctypes.c_int
    lib.byda_frame_get_track_count.argtypes = [ctypes.c_void_p]

    lib.byda_frame_get_dyn_high_point.restype = ctypes.c_int
    lib.byda_frame_get_dyn_high_point.argtypes = [
        ctypes.c_void_p, ctypes.c_int,
        ctypes.POINTER(ctypes.c_float), ctypes.POINTER(ctypes.c_float),
        ctypes.POINTER(ctypes.c_float), ctypes.POINTER(ctypes.c_float),
        ctypes.POINTER(ctypes.c_uint16),
    ]

    lib.byda_frame_get_track_point.restype = ctypes.c_int
    lib.byda_frame_get_track_point.argtypes = [
        ctypes.c_void_p, ctypes.c_int,
        ctypes.POINTER(ctypes.c_float), ctypes.POINTER(ctypes.c_float),
        ctypes.POINTER(ctypes.c_float),
    ]

# ------------------------------------------------------------------ #
# Pythonic wrapper class                                               #
# ------------------------------------------------------------------ #

class Hdr20Radar:
    """
    High-level Python interface for HDR20 (4D Radar) via BSR20 SDK.

    Uses the frame callback API to receive parsed 4D point cloud data.
    """

    def __init__(self, ip: str = "192.168.172.128", port: int = 7):
        self._lib = _load_library()
        _setup_prototypes(self._lib)
        self._handle = self._lib.byda_create()
        if not self._handle:
            raise MemoryError("Failed to create radar context")
        self._lib.byda_set_address(self._handle, ip.encode("utf-8"), port)
        self._connected = False
        self._user_frame_cb = None
        self._c_frame_cb = None  # prevent GC

    def __del__(self):
        self.close()

    def __enter__(self):
        return self

    def __exit__(self, *args):
        self.close()

    def close(self):
        if hasattr(self, "_handle") and self._handle:
            if self._connected:
                self._lib.byda_disconnect(self._handle)
                self._connected = False
            self._lib.byda_destroy(self._handle)
            self._handle = None

    # -- Connection ------------------------------------------------- #

    def connect(self):
        rc = self._lib.byda_connect(self._handle)
        if rc != BYDA_OK:
            raise BydaError(rc)
        self._connected = True

    def disconnect(self):
        if self._connected:
            self._lib.byda_disconnect(self._handle)
            self._connected = False

    # -- Configuration ---------------------------------------------- #

    @staticmethod
    def sdk_version() -> float:
        lib = _load_library()
        _setup_prototypes(lib)
        return lib.byda_get_sdk_version()

    @property
    def comm_version(self) -> int:
        return self._lib.byda_get_comm_version(self._handle)

    # -- Frame callback --------------------------------------------- #

    def set_frame_callback(self, callback: Callable[[FrameInfo], None]):
        """Register a Python callback for 4D radar frames.

        The callback receives a FrameInfo dataclass with all point
        cloud data already extracted from the frame handle.
        """
        self._user_frame_cb = callback

        def _c_callback(frame_handle, _user_ctx):
            lib = self._lib
            info = FrameInfo(
                frame_num=lib.byda_frame_get_num(frame_handle),
                dyn_high_count=lib.byda_frame_get_dyn_high_count(frame_handle),
                dyn_low_count=lib.byda_frame_get_dyn_low_count(frame_handle),
                mm_long_high_count=lib.byda_frame_get_mm_long_high_count(frame_handle),
                mm_long_low_count=lib.byda_frame_get_mm_long_low_count(frame_handle),
                mm_short_high_count=lib.byda_frame_get_mm_short_high_count(frame_handle),
                mm_short_low_count=lib.byda_frame_get_mm_short_low_count(frame_handle),
                track_count=lib.byda_frame_get_track_count(frame_handle),
            )

            # Extract dyn_high points
            x = ctypes.c_float()
            y = ctypes.c_float()
            z = ctypes.c_float()
            vel = ctypes.c_float()
            snr = ctypes.c_uint16()
            for i in range(info.dyn_high_count):
                if lib.byda_frame_get_dyn_high_point(
                    frame_handle, i,
                    ctypes.byref(x), ctypes.byref(y),
                    ctypes.byref(z), ctypes.byref(vel),
                    ctypes.byref(snr)
                ) == BYDA_OK:
                    info.dyn_high_points.append(
                        DynHighPoint(x.value, y.value, z.value, vel.value, snr.value)
                    )

            # Extract track points
            for i in range(info.track_count):
                if lib.byda_frame_get_track_point(
                    frame_handle, i,
                    ctypes.byref(x), ctypes.byref(y), ctypes.byref(z)
                ) == BYDA_OK:
                    info.track_points.append(
                        TrackPoint(x.value, y.value, z.value)
                    )

            self._user_frame_cb(info)

        self._c_frame_cb = _FRAME_CB_TYPE(_c_callback)
        self._lib.byda_set_frame_callback(self._handle, self._c_frame_cb, None)

    # -- Receive loop ----------------------------------------------- #

    def receive_loop(self, running_flag=None):
        """Blocking receive loop. Frames are delivered via callback.

        Args:
            running_flag: optional callable returning bool, e.g. threading.Event().is_set
        """
        import signal
        stop = [False]

        def _sig(s, f):
            stop[0] = True
        old_int = signal.signal(signal.SIGINT, _sig)
        old_term = signal.signal(signal.SIGTERM, _sig)

        try:
            while not stop[0]:
                if running_flag and not running_flag():
                    break
                ret = self._lib.byda_receive(self._handle)
                if ret == 0:
                    print("Connection closed by radar.")
                    break
                if ret < 0:
                    if not stop[0]:
                        print(f"[Error] byda_receive returned {ret}")
                    break
        finally:
            signal.signal(signal.SIGINT, old_int)
            signal.signal(signal.SIGTERM, old_term)

    def receive_once(self) -> int:
        """Single receive call. Returns bytes received."""
        return self._lib.byda_receive(self._handle)


# ------------------------------------------------------------------ #
# Example usage                                                        #
# ------------------------------------------------------------------ #

if __name__ == "__main__":
    print(f"BSR20 SDK Version: {Hdr20Radar.sdk_version()}")

    def on_frame(info: FrameInfo):
        print(f"[Frame {info.frame_num}] dynHigh={info.dyn_high_count} "
              f"tracks={info.track_count}")
        for p in info.dyn_high_points[:5]:
            print(f"  [dyn_high] x={p.x:.2f} y={p.y:.2f} z={p.z:.2f} "
                  f"vel={p.vel:.2f} snr={p.snr}")
        for p in info.track_points[:5]:
            print(f"  [track] x={p.x:.2f} y={p.y:.2f} z={p.z:.2f}")

    ip = sys.argv[1] if len(sys.argv) >= 2 else "192.168.172.128"
    port = int(sys.argv[2]) if len(sys.argv) >= 3 else 7

    with Hdr20Radar(ip, port) as radar:
        radar.set_frame_callback(on_frame)
        print(f"Connecting to {ip}:{port}...")
        radar.connect()
        print(f"Connected! Comm version: {radar.comm_version}")
        print("Press Ctrl+C to quit.")
        radar.receive_loop()
