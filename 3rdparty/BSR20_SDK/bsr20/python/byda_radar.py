"""
BYDA Radar SDK - Python Wrapper
================================
Python ctypes wrapper for the BSR20_SDK shared library (libBSR20_SDK.so / BSR20_SDK.dll).

Usage:
    from byda_radar import BydaRadar

    with BydaRadar("192.168.172.128", 7) as radar:
        radar.connect()

        while True:
            tracks = radar.poll()
            for t in tracks:
                print(f"ID={t.id}, x={t.x_pos:.1f}m, y={t.y_pos:.1f}m")
"""

import ctypes
import ctypes.util
import os
import sys
from dataclasses import dataclass
from typing import List, Optional

# ------------------------------------------------------------------ #
# C-compatible structures (must match byda_c_api.h)                  #
# ------------------------------------------------------------------ #

class _BydaTrack(ctypes.Structure):
    _pack_ = 4
    _fields_ = [
        ("id",     ctypes.c_uint16),
        ("x_pos",  ctypes.c_float),
        ("y_pos",  ctypes.c_float),
        ("x_vel",  ctypes.c_float),
        ("y_vel",  ctypes.c_float),
        ("type",   ctypes.c_uint16),
        ("status", ctypes.c_uint16),
    ]

class _BydaScanInfo(ctypes.Structure):
    _pack_ = 4
    _fields_ = [
        ("scan_index",  ctypes.c_uint32),
        ("timestamp",   ctypes.c_uint32),
        ("track_count", ctypes.c_uint16),
        ("error_flag",  ctypes.c_uint16),
    ]

class _BydaInstallInfo(ctypes.Structure):
    _pack_ = 4
    _fields_ = [
        ("height",       ctypes.c_float),
        ("distance",     ctypes.c_float),
        ("pitch",        ctypes.c_float),
        ("azimuth",      ctypes.c_float),
        ("speed_offset", ctypes.c_float),
        ("road_angle",   ctypes.c_float),
        ("direction",    ctypes.c_uint8),
    ]

class _BydaLaneInfo(ctypes.Structure):
    _pack_ = 4
    _fields_ = [
        ("enable",        ctypes.c_uint8),
        ("type",          ctypes.c_uint8),
        ("direction",     ctypes.c_uint8),
        ("real_lane_num", ctypes.c_uint8),
        ("start_pos",     ctypes.c_float),
        ("width",         ctypes.c_float),
    ]

class _BydaVdsEvent(ctypes.Structure):
    _pack_ = 4
    _fields_ = [
        ("track_id",   ctypes.c_uint16),
        ("lane_num",   ctypes.c_uint16),
        ("speed",      ctypes.c_float),
        ("occ_time",   ctypes.c_uint16),
        ("track_type", ctypes.c_uint16),
        ("y_pos",      ctypes.c_float),
        ("scan_index", ctypes.c_int32),
    ]

class _BydaSerialInfo(ctypes.Structure):
    _pack_ = 4
    _fields_ = [
        ("status", ctypes.c_uint8),
        ("year",   ctypes.c_uint8),
        ("month",  ctypes.c_uint8),
        ("date",   ctypes.c_uint8),
        ("number", ctypes.c_uint16),
    ]

class _BydaSwInfo(ctypes.Structure):
    _pack_ = 4
    _fields_ = [
        ("boot_ver",     ctypes.c_uint8),
        ("kernel_ver",   ctypes.c_uint8),
        ("firmware_ver", ctypes.c_uint8),
        ("sp_lib_ver",   ctypes.c_uint8),
    ]

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
# Python dataclasses (user-facing, immutable copies)                  #
# ------------------------------------------------------------------ #

@dataclass
class Track:
    id: int
    x_pos: float
    y_pos: float
    x_vel: float
    y_vel: float
    type: int
    status: int

@dataclass
class ScanInfo:
    scan_index: int
    timestamp: int
    track_count: int
    error_flag: int

@dataclass
class InstallInfo:
    height: float
    distance: float
    pitch: float
    azimuth: float
    speed_offset: float
    road_angle: float
    direction: int

@dataclass
class LaneInfo:
    enable: bool
    type: int
    direction: int
    real_lane_num: int
    start_pos: float
    width: float

@dataclass
class VdsEvent:
    track_id: int
    lane_num: int
    speed: float
    occ_time: int
    track_type: int
    y_pos: float
    scan_index: int

@dataclass
class SerialInfo:
    status: int
    year: int
    month: int
    date: int
    number: int

@dataclass
class SwInfo:
    boot_ver: int
    kernel_ver: int
    firmware_ver: int
    sp_lib_ver: int

# ------------------------------------------------------------------ #
# Error codes                                                         #
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
    """Raised when a C API call returns an error code."""
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
# Library loader                                                      #
# ------------------------------------------------------------------ #

def _load_library() -> ctypes.CDLL:
    """Load the BSR20_SDK shared library."""
    names = []
    script_dir = os.path.dirname(os.path.abspath(__file__))
    sample_dir = os.path.normpath(os.path.join(script_dir, "..", ".."))

    if sys.platform == "win32":
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
            "BSR20_SDK",
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
            "BSR20_SDK",
        ]

    for name in names:
        try:
            return ctypes.CDLL(name)
        except OSError:
            continue

    # Try ctypes.util as last resort
    path = ctypes.util.find_library("BSR20_SDK")
    if path:
        return ctypes.CDLL(path)

    raise OSError(
        "Cannot find BSR20_SDK shared library. "
        "Place libBSR20_SDK.so (Linux) or BSR20_SDK.dll (Windows) "
        "in the same directory as this script, or set LD_LIBRARY_PATH / PATH."
    )

def _setup_prototypes(lib: ctypes.CDLL):
    """Declare C function signatures for type safety."""
    c_handle = ctypes.c_void_p

    # Lifecycle
    lib.byda_create.restype = c_handle
    lib.byda_create.argtypes = []

    lib.byda_destroy.restype = None
    lib.byda_destroy.argtypes = [c_handle]

    # Configuration
    lib.byda_set_address.restype = ctypes.c_int
    lib.byda_set_address.argtypes = [c_handle, ctypes.c_char_p, ctypes.c_int]

    lib.byda_set_vds_config.restype = ctypes.c_int
    lib.byda_set_vds_config.argtypes = [c_handle, ctypes.POINTER(_BydaVdsConfig)]

    lib.byda_get_sdk_version.restype = ctypes.c_float
    lib.byda_get_sdk_version.argtypes = []

    lib.byda_get_comm_version.restype = ctypes.c_int
    lib.byda_get_comm_version.argtypes = [c_handle]

    # Connection
    lib.byda_connect.restype = ctypes.c_int
    lib.byda_connect.argtypes = [c_handle]

    lib.byda_disconnect.restype = None
    lib.byda_disconnect.argtypes = [c_handle]

    lib.byda_reset.restype = ctypes.c_int
    lib.byda_reset.argtypes = [c_handle]

    # Data loop
    lib.byda_receive.restype = ctypes.c_int
    lib.byda_receive.argtypes = [c_handle]

    lib.byda_process.restype = ctypes.c_int
    lib.byda_process.argtypes = [c_handle]

    lib.byda_poll.restype = ctypes.c_int
    lib.byda_poll.argtypes = [c_handle]

    # Track accessors
    lib.byda_get_scan_info.restype = ctypes.c_int
    lib.byda_get_scan_info.argtypes = [c_handle, ctypes.POINTER(_BydaScanInfo)]

    lib.byda_get_track_count.restype = ctypes.c_int
    lib.byda_get_track_count.argtypes = [c_handle]

    lib.byda_get_track.restype = ctypes.c_int
    lib.byda_get_track.argtypes = [c_handle, ctypes.c_int, ctypes.POINTER(_BydaTrack)]

    lib.byda_get_tracks.restype = ctypes.c_int
    lib.byda_get_tracks.argtypes = [c_handle, ctypes.POINTER(_BydaTrack), ctypes.c_int]

    # VDS accessors
    lib.byda_get_vds_count.restype = ctypes.c_int
    lib.byda_get_vds_count.argtypes = [c_handle]

    lib.byda_get_vds_event.restype = ctypes.c_int
    lib.byda_get_vds_event.argtypes = [c_handle, ctypes.c_int, ctypes.POINTER(_BydaVdsEvent)]

    # Parameter load
    lib.byda_load_install.restype = ctypes.c_int
    lib.byda_load_install.argtypes = [c_handle]

    lib.byda_load_lane.restype = ctypes.c_int
    lib.byda_load_lane.argtypes = [c_handle]

    lib.byda_load_detect.restype = ctypes.c_int
    lib.byda_load_detect.argtypes = [c_handle]

    lib.byda_load_serial.restype = ctypes.c_int
    lib.byda_load_serial.argtypes = [c_handle]

    lib.byda_load_sw_info.restype = ctypes.c_int
    lib.byda_load_sw_info.argtypes = [c_handle]

    lib.byda_is_param_idle.restype = ctypes.c_int
    lib.byda_is_param_idle.argtypes = [c_handle]

    # Parameter getters
    lib.byda_get_install_info.restype = ctypes.c_int
    lib.byda_get_install_info.argtypes = [c_handle, ctypes.POINTER(_BydaInstallInfo)]

    lib.byda_get_lane_count.restype = ctypes.c_int
    lib.byda_get_lane_count.argtypes = [c_handle]

    lib.byda_get_lane_info.restype = ctypes.c_int
    lib.byda_get_lane_info.argtypes = [c_handle, ctypes.c_int, ctypes.POINTER(_BydaLaneInfo)]

    lib.byda_get_detect_line.restype = ctypes.c_int
    lib.byda_get_detect_line.argtypes = [c_handle, ctypes.POINTER(ctypes.c_float)]

    lib.byda_get_serial_info.restype = ctypes.c_int
    lib.byda_get_serial_info.argtypes = [c_handle, ctypes.POINTER(_BydaSerialInfo)]

    lib.byda_get_sw_info.restype = ctypes.c_int
    lib.byda_get_sw_info.argtypes = [c_handle, ctypes.POINTER(_BydaSwInfo)]

    # Parameter setters
    lib.byda_set_install.restype = ctypes.c_int
    lib.byda_set_install.argtypes = [
        c_handle, ctypes.c_float, ctypes.c_float, ctypes.c_float,
        ctypes.c_float, ctypes.c_float, ctypes.c_float, ctypes.c_uint8,
    ]

    lib.byda_set_detect_line.restype = ctypes.c_int
    lib.byda_set_detect_line.argtypes = [c_handle, ctypes.c_float]

# ------------------------------------------------------------------ #
# Pythonic wrapper class                                              #
# ------------------------------------------------------------------ #

class BydaRadar:
    """
    High-level Python interface for the BYDA radar SDK.

    Example::

        with BydaRadar("192.168.172.128", 7) as radar:
            radar.connect()
            while True:
                tracks = radar.poll()
                for t in tracks:
                    print(f"ID={t.id} pos=({t.x_pos:.1f}, {t.y_pos:.1f})")
    """

    def __init__(self, ip: str = "192.168.172.128", port: int = 7):
        self._lib = _load_library()
        _setup_prototypes(self._lib)
        self._handle = self._lib.byda_create()
        if not self._handle:
            raise MemoryError("Failed to create radar context")
        self._lib.byda_set_address(self._handle, ip.encode("utf-8"), port)
        self._connected = False

    def __del__(self):
        self.close()

    def __enter__(self):
        return self

    def __exit__(self, *args):
        self.close()

    def close(self):
        """Release all resources."""
        if hasattr(self, "_handle") and self._handle:
            if self._connected:
                self._lib.byda_disconnect(self._handle)
                self._connected = False
            self._lib.byda_destroy(self._handle)
            self._handle = None

    # -- Connection ------------------------------------------------- #

    def connect(self):
        """Connect to the radar (blocking)."""
        rc = self._lib.byda_connect(self._handle)
        if rc != BYDA_OK:
            raise BydaError(rc)
        self._connected = True

    def disconnect(self):
        """Disconnect from the radar."""
        if self._connected:
            self._lib.byda_disconnect(self._handle)
            self._connected = False

    def reset(self):
        """Send system reset command."""
        rc = self._lib.byda_reset(self._handle)
        if rc != BYDA_OK:
            raise BydaError(rc)

    # -- Configuration ---------------------------------------------- #

    @staticmethod
    def sdk_version() -> float:
        """Get SDK version number."""
        lib = _load_library()
        _setup_prototypes(lib)
        return lib.byda_get_sdk_version()

    @property
    def comm_version(self) -> int:
        """Communication protocol version (available after connect)."""
        return self._lib.byda_get_comm_version(self._handle)

    def set_vds_config(self, det_line_up: float = 40.0, det_line_dn: float = 40.0,
                       up_band_st: float = 0.0, up_band_width: float = 3.0,
                       dn_band_st: float = 0.0, dn_band_width: float = 3.0,
                       car2car_time: float = 0.8):
        """Configure VDS detection parameters."""
        cfg = _BydaVdsConfig(det_line_up, det_line_dn,
                             up_band_st, up_band_width,
                             dn_band_st, dn_band_width, car2car_time)
        rc = self._lib.byda_set_vds_config(self._handle, ctypes.byref(cfg))
        if rc != BYDA_OK:
            raise BydaError(rc)

    # -- Data loop -------------------------------------------------- #

    def receive(self) -> int:
        """Receive raw data from radar (blocking). Returns bytes received."""
        return self._lib.byda_receive(self._handle)

    def process(self) -> int:
        """Process received data. Returns track count."""
        return self._lib.byda_process(self._handle)

    def poll(self) -> List[Track]:
        """Receive + process + return tracks (convenience method)."""
        rc = self._lib.byda_poll(self._handle)
        if rc < 0:
            raise BydaError(rc)
        if rc == 0:
            return []
        return self.get_tracks()

    # -- Track data ------------------------------------------------- #

    def get_scan_info(self) -> ScanInfo:
        """Get metadata for the last processed scan frame."""
        info = _BydaScanInfo()
        rc = self._lib.byda_get_scan_info(self._handle, ctypes.byref(info))
        if rc != BYDA_OK:
            raise BydaError(rc)
        return ScanInfo(info.scan_index, info.timestamp,
                        info.track_count, info.error_flag)

    def get_track_count(self) -> int:
        """Get number of decoded tracks."""
        return self._lib.byda_get_track_count(self._handle)

    def get_track(self, index: int) -> Track:
        """Get a single track by index."""
        t = _BydaTrack()
        rc = self._lib.byda_get_track(self._handle, index, ctypes.byref(t))
        if rc != BYDA_OK:
            raise BydaError(rc)
        return Track(t.id, t.x_pos, t.y_pos, t.x_vel, t.y_vel, t.type, t.status)

    def get_tracks(self) -> List[Track]:
        """Get all tracks at once."""
        count = self.get_track_count()
        if count <= 0:
            return []
        arr = (_BydaTrack * count)()
        n = self._lib.byda_get_tracks(self._handle, arr, count)
        if n < 0:
            raise BydaError(n)
        return [
            Track(arr[i].id, arr[i].x_pos, arr[i].y_pos,
                  arr[i].x_vel, arr[i].y_vel, arr[i].type, arr[i].status)
            for i in range(n)
        ]

    # -- VDS events ------------------------------------------------- #

    def get_vds_events(self) -> List[VdsEvent]:
        """Get VDS detection events from last process cycle."""
        count = self._lib.byda_get_vds_count(self._handle)
        if count <= 0:
            return []
        events = []
        for i in range(count):
            e = _BydaVdsEvent()
            rc = self._lib.byda_get_vds_event(self._handle, i, ctypes.byref(e))
            if rc != BYDA_OK:
                break
            events.append(VdsEvent(e.track_id, e.lane_num, e.speed,
                                   e.occ_time, e.track_type, e.y_pos, e.scan_index))
        return events

    # -- Parameter loading ------------------------------------------ #

    def is_param_idle(self) -> bool:
        """Check if parameter state machine is idle (ready for next load)."""
        return self._lib.byda_is_param_idle(self._handle) == 1

    def load_install(self):
        """Request installation parameters from radar."""
        rc = self._lib.byda_load_install(self._handle)
        if rc != BYDA_OK:
            raise BydaError(rc)

    def load_lane(self):
        """Request lane parameters from radar."""
        rc = self._lib.byda_load_lane(self._handle)
        if rc != BYDA_OK:
            raise BydaError(rc)

    def load_detect(self):
        """Request detection line parameters from radar."""
        rc = self._lib.byda_load_detect(self._handle)
        if rc != BYDA_OK:
            raise BydaError(rc)

    def load_serial(self):
        """Request product serial number from radar."""
        rc = self._lib.byda_load_serial(self._handle)
        if rc != BYDA_OK:
            raise BydaError(rc)

    def load_sw_info(self):
        """Request software version info from radar."""
        rc = self._lib.byda_load_sw_info(self._handle)
        if rc != BYDA_OK:
            raise BydaError(rc)

    # -- Parameter getters ------------------------------------------ #

    def get_install_info(self) -> InstallInfo:
        """Get installation parameters (call load_install() first)."""
        info = _BydaInstallInfo()
        rc = self._lib.byda_get_install_info(self._handle, ctypes.byref(info))
        if rc != BYDA_OK:
            raise BydaError(rc)
        return InstallInfo(info.height, info.distance, info.pitch,
                           info.azimuth, info.speed_offset,
                           info.road_angle, info.direction)

    def get_lane_count(self) -> int:
        """Get total number of configured lanes."""
        return self._lib.byda_get_lane_count(self._handle)

    def get_lane_info(self, index: int) -> LaneInfo:
        """Get lane info by index (call load_lane() first)."""
        info = _BydaLaneInfo()
        rc = self._lib.byda_get_lane_info(self._handle, index, ctypes.byref(info))
        if rc != BYDA_OK:
            raise BydaError(rc)
        return LaneInfo(bool(info.enable), info.type, info.direction,
                        info.real_lane_num, info.start_pos, info.width)

    def get_lanes(self) -> List[LaneInfo]:
        """Get all lanes at once."""
        count = self.get_lane_count()
        return [self.get_lane_info(i) for i in range(count)]

    def get_detect_line(self) -> float:
        """Get detection line position in meters (call load_detect() first)."""
        val = ctypes.c_float()
        rc = self._lib.byda_get_detect_line(self._handle, ctypes.byref(val))
        if rc != BYDA_OK:
            raise BydaError(rc)
        return val.value

    def get_serial_info(self) -> SerialInfo:
        """Get product serial info (call load_serial() first)."""
        info = _BydaSerialInfo()
        rc = self._lib.byda_get_serial_info(self._handle, ctypes.byref(info))
        if rc != BYDA_OK:
            raise BydaError(rc)
        return SerialInfo(info.status, info.year, info.month,
                          info.date, info.number)

    def get_sw_info(self) -> SwInfo:
        """Get software version info (call load_sw_info() first)."""
        info = _BydaSwInfo()
        rc = self._lib.byda_get_sw_info(self._handle, ctypes.byref(info))
        if rc != BYDA_OK:
            raise BydaError(rc)
        return SwInfo(info.boot_ver, info.kernel_ver,
                      info.firmware_ver, info.sp_lib_ver)

    # -- Parameter setters ------------------------------------------ #

    def set_install(self, height: float, distance: float, pitch: float,
                    azimuth: float, speed_offset: float = 0.0,
                    road_angle: float = 0.0, direction: int = 0):
        """Set installation parameters on the radar."""
        rc = self._lib.byda_set_install(
            self._handle, height, distance, pitch, azimuth,
            speed_offset, road_angle, direction)
        if rc != BYDA_OK:
            raise BydaError(rc)

    def set_detect_line(self, pos: float):
        """Set detection line position (meters)."""
        rc = self._lib.byda_set_detect_line(self._handle, pos)
        if rc != BYDA_OK:
            raise BydaError(rc)


# ------------------------------------------------------------------ #
# Example usage                                                       #
# ------------------------------------------------------------------ #

if __name__ == "__main__":
    print(f"BSR20 SDK Version: {BydaRadar.sdk_version()}")

    with BydaRadar("192.168.172.128", 7) as radar:
        print("Connecting...")
        radar.connect()
        print(f"Connected! Comm version: {radar.comm_version}")

        # Load parameters
        radar.load_install()
        cycle = 0

        while True:
            n = radar.receive()
            if n <= 0:
                continue

            track_count = radar.process()
            cycle += 1

            # Check if install params arrived
            if radar.is_param_idle() and cycle == 1:
                try:
                    info = radar.get_install_info()
                    print(f"Install: H={info.height}m, D={info.distance}m, "
                          f"Pitch={info.pitch}deg")
                except BydaError:
                    pass

                # Load lanes next
                radar.load_lane()

            # Print tracks
            if track_count > 0:
                scan = radar.get_scan_info()
                print(f"[Scan {scan.scan_index}] {track_count} tracks")

                for t in radar.get_tracks():
                    print(f"  ID={t.id:3d} pos=({t.x_pos:6.1f}, {t.y_pos:6.1f}) "
                          f"vel=({t.x_vel:5.1f}, {t.y_vel:5.1f}) "
                          f"type={t.type} status={t.status}")

            # Print VDS events
            for ev in radar.get_vds_events():
                print(f"  [VDS] Lane={ev.lane_num} ID={ev.track_id} "
                      f"Speed={ev.speed:.1f}kph Type={ev.track_type}")
