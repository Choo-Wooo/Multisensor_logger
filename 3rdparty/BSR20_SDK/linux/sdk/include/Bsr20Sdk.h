/*=============================================================================
 * BYDA Radar SDK - C ABI Interface
 *
 * C ABI wrapper for the BYDA radar SDK.
 * Build as shared library (.so / .dll) and use from any language via FFI.
 *
 * ---------------------------------------------------------------------------
 * BSR10 / BSR20 PRODUCT API
 * ---------------------------------------------------------------------------
 * This header preserves the existing BSR C ABI. Call byda_receive() followed
 * by byda_process(), or use byda_poll() to perform both operations. HDR10 uses
 * the separate Hdr10Sdk.h API while remaining in the same shared library.
 *
 * ---------------------------------------------------------------------------
 * THREADING
 * ---------------------------------------------------------------------------
 * A handle is NOT thread-safe. Callbacks fire synchronously on the thread
 * that calls byda_receive()/byda_process(). Do not call into the same handle
 * from another thread, and in particular do not overlap create/destroy with
 * receive. Multiple handles may run on separate threads, but socket/WSA
 * startup and teardown is process-wide - create and destroy handles from a
 * single owning thread.
 *
 * ---------------------------------------------------------------------------
 * Usage (C):
 *   BydaRadarHandle h = byda_create();
 *   byda_set_address(h, "192.168.172.128", 7);
 *   byda_connect(h);
 *   while (1) {
 *       if (byda_receive(h) > 0) {
 *           int n = byda_process(h);
 *           for (int i = 0; i < n; i++) {
 *               BydaTrack t;
 *               byda_get_track(h, i, &t);
 *           }
 *       }
 *   }
 *   byda_disconnect(h);
 *   byda_destroy(h);
 *=============================================================================*/

#ifndef BYDA_C_API_H
#define BYDA_C_API_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/* ------------------------------------------------------------------ */
/* Export / Import macros                                              */
/* ------------------------------------------------------------------ */
#ifdef _WIN32
    #ifdef BSR20_SDK_EXPORTS
        #define BYDA_API __declspec(dllexport)
    #else
        #define BYDA_API __declspec(dllimport)
    #endif
#else
    #define BYDA_API __attribute__((visibility("default")))
#endif

/* ------------------------------------------------------------------ */
/* Opaque handle                                                      */
/* ------------------------------------------------------------------ */
typedef void* BydaRadarHandle;

/* ------------------------------------------------------------------ */
/* Error codes                                                        */
/* ------------------------------------------------------------------ */
#define BYDA_OK                 0
#define BYDA_ERR_NULL_HANDLE   -1
#define BYDA_ERR_CONNECT       -2
#define BYDA_ERR_RECV          -3
#define BYDA_ERR_NO_DATA       -4
#define BYDA_ERR_INVALID_INDEX -5
#define BYDA_ERR_PARAM_BUSY    -6
#define BYDA_ERR_NOT_CONNECTED -7
#define BYDA_ERR_UNSUPPORTED_VERSION -8
#define BYDA_ERR_PROTOCOL      -9
#define BYDA_ERR_TIMEOUT      -10

/* Communication Version byte returned by the 0x45A0 handshake. */
#define BYDA_COMM_VERSION_0 0
#define BYDA_COMM_VERSION_1 1  /* legacy layout, same SDK path as v0 */
#define BYDA_COMM_VERSION_2 2
#define BYDA_COMM_VERSION_3 3
#define BYDA_V3_LANE_SEGMENT_COUNT 7
#define BYDA_DETECTION_RANGE_MAX_COUNT 4
#define BYDA_DETECTION_RANGE_POINT_COUNT 8

/* Last socket receive outcome. socket_error is non-zero only for
 * BYDA_RECEIVE_KIND_SOCKET_ERROR and contains WSAGetLastError() on Windows or
 * errno on POSIX. */
#define BYDA_RECEIVE_KIND_IDLE         0U
#define BYDA_RECEIVE_KIND_DATA         1U
#define BYDA_RECEIVE_KIND_TIMEOUT      2U
#define BYDA_RECEIVE_KIND_PEER_CLOSED  3U
#define BYDA_RECEIVE_KIND_SOCKET_ERROR 4U

/* ------------------------------------------------------------------ */
/* C-compatible data structures (all values in physical units)        */
/* ------------------------------------------------------------------ */
#pragma pack(push, 4)

typedef struct {
    uint32_t receive_kind;  /* BYDA_RECEIVE_KIND_* */
    int32_t  socket_error;  /* WSA error / errno, or 0 */
} BydaReceiveDiagnostic;

/** Decoded track object (position in meters, velocity scaled). */
typedef struct {
    uint16_t id;
    float    x_pos;       /* meters   (raw / 8.0)  */
    float    y_pos;       /* meters   (raw / 8.0)  */
    float    x_vel;       /* velocity (raw / 2.0)  */
    float    y_vel;       /* velocity (raw / 8.0)  */
    uint16_t type;        /* object type  (0-7)    */
    uint16_t status;      /* object status (0-31)  */
} BydaTrack;

/** Scan frame metadata. */
typedef struct {
    uint32_t scan_index;
    uint32_t timestamp;
    uint16_t track_count;  /* object tracks (excluding system data) */
    uint16_t error_flag;
} BydaScanInfo;

/** Installation parameters (all in physical units). */
typedef struct {
    float   height;        /* meters       */
    float   distance;      /* meters       */
    float   pitch;         /* degrees      */
    float   azimuth;       /* degrees      */
    float   speed_offset;  /* offset value */
    float   road_angle;    /* degrees      */
    uint8_t direction;     /* 0: Up, 1: Down */
} BydaInstallInfo;

/** Lane information (unified across protocol versions). */
typedef struct {
    uint8_t enable;
    uint8_t type;          /* 0: Normal, 1: Bus, 2: Side */
    uint8_t direction;     /* 0: Up, 1: Down, 2: UpDown  */
    uint8_t real_lane_num;
    float   start_pos;     /* meters */
    float   width;         /* meters */
} BydaLaneInfo;

/** One communication-v3 curved-lane segment. */
typedef struct {
    uint8_t enable;
    float   left;           /* meters, 0.1 m resolution */
    float   right;          /* meters, 0.1 m resolution */
    float   range;          /* meters, 1 m resolution */
} BydaLaneV3Segment;

/** Communication-v3 lane information including curved-lane geometry. */
typedef struct {
    BydaLaneInfo lane;
    uint8_t real_lane_direction; /* 0 or 1 */
    uint8_t lane_turn_direction; /* 0..7 */
    BydaLaneV3Segment segments[BYDA_V3_LANE_SEGMENT_COUNT];
} BydaLaneV3Info;

/** Detection line info. */
typedef struct {
    uint8_t  enable;       /* 0=disabled, 1=enabled */
    uint8_t  direction;    /* 0:Up, 1:Down, 2:UpDown (v2 packet only) */
    float    position;     /* detection line position in meters */
} BydaDetectLineInfo;

/** One point in a detection-range polygon. */
typedef struct {
    uint8_t enable;
    float   x;              /* meters */
    float   y;              /* meters */
} BydaDetectionRangePoint;

/** Detection range in physical units.
 *  commVer 0/1 uses x_min/x_max/y_min/y_max.
 *  commVer 2/3 uses enable/property/points; index 0 is the include polygon
 *  and indexes 1..3 are exclude polygons. */
typedef struct {
    uint8_t enable;
    uint8_t property;       /* 0=include, 1=exclude */
    float   x_min;          /* meters; commVer 0/1 */
    float   x_max;          /* meters; commVer 0/1 */
    float   y_min;          /* meters; commVer 0/1 */
    float   y_max;          /* meters; commVer 0/1 */
    BydaDetectionRangePoint points[BYDA_DETECTION_RANGE_POINT_COUNT];
    float   speed_min1;     /* kph, uphill */
    float   speed_max1;     /* kph, uphill */
    float   speed_min2;     /* kph, downhill */
    float   speed_max2;     /* kph, downhill */
} BydaDetectionRangeInfo;

/** Product serial number. */
typedef struct {
    uint8_t  status;
    uint8_t  year;
    uint8_t  month;
    uint8_t  date;
    uint16_t number;
} BydaSerialInfo;

/** Software version info. */
typedef struct {
    uint8_t boot_ver;
    uint8_t kernel_ver;
    uint8_t firmware_ver;
    uint8_t sp_lib_ver;
} BydaSwInfo;

/* ------------------------------------------------------------------ */
/* Radar Control parameter — bit positions for func_enable bitfield   */
/* ------------------------------------------------------------------ */
/** 32 toggle bits for BydaRadarControlInfo::func_enable.
 *  Each entry maps 1:1 to the firmware-side radarParamFuncEnSt bit and the
 *  device's UI checkbox label (in Korean, as shown on the configurator).
 *
 *  ★ Semantics note: bits whose firmware name ends with "Off" are inverted —
 *  setting them to 1 *disables* the underlying feature. Marked "(1=off)" below.
 *  ★ Bit 11 (init_SettingOn in packet) is exposed via a separate factory-reset
 *  API, not this bitfield. */
typedef enum {
    BYDA_RC_FUNC_FREQ_ST_TILT          = 0,  /* UI "트리거 개선 적용" */
    BYDA_RC_FUNC_DFS_OUT_FILTER        = 1,  /* UI "출력 필터링 (DFS)" */
    BYDA_RC_FUNC_RM_TRIG_SPD_DIFF_OFF  = 2,  /* UI "스피드 필터 해제"   (1=off) */
    BYDA_RC_FUNC_RM_NEAR_GRATING       = 3,  /* UI "근거리 필터 적용" */
    BYDA_RC_FUNC_CHANNEL_FILT_ON       = 4,  /* UI "채널필터 적용" */
    BYDA_RC_FUNC_INIT_SYS_INFO_ON      = 5,  /* UI "예비기능_5"  (sys info init) */
    BYDA_RC_FUNC_RESERVED_6            = 6,  /* UI "예비기능_6" */
    BYDA_RC_FUNC_OBJ_FILTER_TH_INC     = 7,  /* UI "대형차량적응 적용" */
    BYDA_RC_FUNC_INTER_SVHC_INSERT_ON  = 8,  /* UI "감지개선필터 적용" */
    BYDA_RC_FUNC_DETLINE_SVHC_INSERT   = 9,  /* UI "측정치필터 해제" */
    BYDA_RC_FUNC_DOUT_SPD_COMP_OFF     = 10, /* UI "출력속도보정 해제" (1=off) */
    BYDA_RC_FUNC_CURVE_FIT_ON          = 11, /* UI "MP필터 해제" */
    BYDA_RC_FUNC_SRR_OBJ_RESTRICT_ON   = 12, /* UI "BM필터 해제" */
    BYDA_RC_FUNC_CFAR_OS_ON            = 13, /* UI "CF_2 해제" */
    BYDA_RC_FUNC_CFAR_MIMO_ON          = 14, /* UI "CF1 해제"  (Chamber용) */
    BYDA_RC_FUNC_LR_SR_SPLIT_ON        = 15, /* UI "속도필터 타겟제거 적용" */
    BYDA_RC_FUNC_DUP_TRK_RANGE_ENLARGE = 16, /* UI "속도필터_1 해제" */
    BYDA_RC_FUNC_LANE_DIR_REJECT_OFF   = 17, /* UI "속도필터_2 해제" (1=off) */
    BYDA_RC_FUNC_ANTI_VEL_GHOST_OFF    = 18, /* UI "예비기능_18"  (1=off) */
    BYDA_RC_FUNC_CIRCLE_REF_OFF        = 19, /* UI "예비기능_19"  (1=off) */
    BYDA_RC_FUNC_OBJ_LOGGING_ON        = 20, /* UI "예비기능_20"  (obj logging) */
    BYDA_RC_FUNC_DOUBLE_REF_OFF        = 21, /* UI "예비기능_21"  (1=off) */
    BYDA_RC_FUNC_LOG_FFT_CPX           = 22, /* UI "예비기능_22"  (FFT cpx log) */
    BYDA_RC_FUNC_LOG_FFT_REAL          = 23, /* UI "예비기능_23"  (FFT real log) */
    BYDA_RC_FUNC_LOG_PEAK              = 24, /* UI "예비기능_24"  (peak log) */
    BYDA_RC_FUNC_SRR_OBJ_DISABLE       = 25, /* UI "예비기능_25"  (SRR force-off) */
    BYDA_RC_FUNC_LRR_OBJ_DISABLE       = 26, /* UI "예비기능_26"  (LRR force-off) */
    BYDA_RC_FUNC_FREQ_CERTI_TX_OFF     = 27, /* UI "예비기능_27"  (1=off) */
    BYDA_RC_FUNC_VEHICLE_SPD_ZERO_ON   = 28, /* UI "예비기능_28"  (force 0kph) */
    BYDA_RC_FUNC_DISABLE_BASIC_OFF     = 29, /* UI "예비기능_29"  ★ 1=mode_num 자동튜닝 비활성 */
    BYDA_RC_FUNC_TEST_TRACK            = 30, /* UI "테스트 트랙 출력" */
    BYDA_RC_FUNC_TEST_SP               = 31  /* UI "테스트 속도 출력" */
} BydaRadarFuncBit;

#define BYDA_RC_FUNC_BIT(b)  ((uint32_t)1u << (b))   /* helper to compose flags */

/** Radar Control parameter (all 32 bytes of the firmware packet, decoded).
 *
 *  Read via byda_get_radar_control() after byda_load_radar_control() completes.
 *  Write via byda_set_radar_control() — only fields the caller wants to change
 *  should be modified; recommended flow is load → modify → set.
 *
 *  ★ Caveat: firmware applies a mode_num-based auto-tune that may overwrite
 *  det_immunity / trk_create_condition / trk_holding_ability immediately after
 *  SET. To prevent this, set bit BYDA_RC_FUNC_DISABLE_BASIC_OFF (bit 29) in
 *  func_enable, OR call set then load to read back the effective values. */
typedef struct {
    /* Enable Control */
    uint16_t ver;                  /* must equal RADAR_CONTROL_PARAM_VERSION (=2); SDK auto-fills on set */
    uint8_t  enable;               /* 0=ignore packet, 1=apply (SDK auto-sets to 1 on set) */

    /* Detection Ability — may be overridden by firmware based on mode_num */
    uint8_t  det_sensitivity;      /* 0~5 */
    uint8_t  det_immunity;         /* 0~7 */
    uint8_t  trk_create_condition;
    uint8_t  trk_holding_ability;

    /* Parameter Setting */
    uint8_t  dev_num;
    uint8_t  ovspd_lim;            /* over-speed limit (kph) */
    uint8_t  app_num;              /* 0~8 */
    uint8_t  mode_num;             /* 0:normal / 1:highway / 2:BMT speed / 3:other */
    uint8_t  disp_num;             /* raw bitfield byte (see radarParamDispModeSt) */
    uint8_t  tuning_param0;        /* app-dependent (named reserved_param_3 on the wire) */
    uint8_t  reserved_param_4;
    uint8_t  reserved_param_5;

    /* Function Setting — 32 toggle bits, indexed by BydaRadarFuncBit enum */
    uint32_t func_enable;
} BydaRadarControlInfo;

/** Radar device network configuration (IP / Subnet / Gateway / Port / MAC).
 *
 *  Read via byda_load_radar_network() + byda_get_radar_network().
 *  Write via byda_set_radar_network() — the SDK echoes all model/serial/fw
 *  fields from the LOAD cache, so byda_load_radar_network() MUST have completed
 *  successfully first (otherwise byda_set_radar_network returns BYDA_ERR_NO_DATA).
 *
 *  ★ Each change_* flag controls whether the corresponding caller value is
 *  applied. On the wire the SDK always sends the complete cached network/MAC
 *  block with every write flag enabled, regardless of commVer: the firmware
 *  erases any field whose write flag is 0 and reverts it to the factory
 *  default on reboot.
 *
 *  ★ After byda_set_radar_network() the radar still runs on the OLD config
 *  until reboot. Call byda_reset() (or power-cycle the radar) to activate.
 *
 *  ★ MAC address changes go through a separate API (byda_set_radar_mac) to
 *  avoid accidental MAC writes during normal IP-change operations. */
typedef struct {
    /* Writable — network settings to change on the radar. */
    uint8_t  ip[4];          /* e.g. 192.168.172.128 → {192, 168, 172, 128} */
    uint8_t  subnet[4];      /* e.g. 255.255.255.0   → {255, 255, 255,   0} */
    uint8_t  gateway[4];
    uint16_t port;           /* TCP port the radar listens on */

    /* "Apply this field" flags — 1=apply, 0=firmware ignores. */
    uint8_t  change_ip;
    uint8_t  change_subnet;
    uint8_t  change_gateway;

    /* Read-only. Filled by byda_get_radar_network() from the LOAD cache;
     * ignored by byda_set_radar_network() — MAC changes go through
     * byda_set_radar_mac() to keep them explicit. */
    uint8_t  mac[6];
} BydaRadarNetworkInfo;

#pragma pack(pop)

/* ------------------------------------------------------------------ */
/* Lifecycle                                                          */
/* ------------------------------------------------------------------ */

/** Create a new radar context. Returns NULL on failure. */
BYDA_API BydaRadarHandle byda_create(void);

/** Destroy a radar context and free all resources. */
BYDA_API void byda_destroy(BydaRadarHandle h);

/* ------------------------------------------------------------------ */
/* Configuration                                                      */
/* ------------------------------------------------------------------ */

/** Set radar IP address and port. Must be called before byda_connect().
 *  Default: "192.168.172.128", port 7 */
BYDA_API int byda_set_address(BydaRadarHandle h, const char* ip, int port);

/** Get SDK version number. */
BYDA_API float byda_get_sdk_version(void);

/** Get the raw communication protocol version reported by the radar.
 *  Supported values are 0, 1, 2, and 3. Version 1 uses the legacy v0 packet
 *  layout; versions 2 and 3 have distinct parameter packet layouts. */
BYDA_API int byda_get_comm_version(BydaRadarHandle h);

/** Get the device function version reported in the connection reply. */
BYDA_API int byda_get_device_function_number(BydaRadarHandle h);

/** Check if connected radar is in relay mode (0x55AA handshake).
 *  Returns 1 if relay mode, 0 otherwise. */
BYDA_API int byda_is_relay_mode(BydaRadarHandle h);

/* ------------------------------------------------------------------ */
/* Connection                                                         */
/* ------------------------------------------------------------------ */

/** Connect to radar. Blocking: sends connection request and waits for reply.
 *  Returns BYDA_OK on success. */
BYDA_API int byda_connect(BydaRadarHandle h);

/** Disconnect from radar. Sends connection end packet and closes socket. */
BYDA_API void byda_disconnect(BydaRadarHandle h);

/** Send system reset command to radar and wait (up to 5 s) for the radar to
 *  drop the TCP connection — that is how the firmware acknowledges the reset
 *  (it resets its Ethernet chip ~1 s after accepting the command).
 *
 *  Returns BYDA_OK once the connection dropped. The handle is then
 *  disconnected: byda_disconnect() becomes a no-op and byda_connect() can be
 *  called again after the radar has rebooted (allow several seconds).
 *  Returns BYDA_ERR_TIMEOUT if the radar kept the connection, i.e. the reset
 *  was not accepted; the handle stays connected in that case.
 *
 *  ★ Never close the connection right behind the reset yourself: the
 *  firmware reads pending commands only while the socket is ESTABLISHED, so
 *  an immediate CONNECTION_END/close silently discards the reset. */
BYDA_API int byda_reset(BydaRadarHandle h);

/* ------------------------------------------------------------------ */
/* Data reception loop                                                */
/* ------------------------------------------------------------------ */

/** Receive raw data from radar (blocking TCP recv).
 *  Returns bytes received (>0), 0 for timeout or orderly peer close, and
 *  BYDA_ERR_RECV for a socket error. Use
 *  byda_get_last_receive_diagnostic() to distinguish the zero cases and read
 *  the native socket error code. */
BYDA_API int byda_receive(BydaRadarHandle h);

/** Get the outcome of the most recent socket receive operation. */
BYDA_API int byda_get_last_receive_diagnostic(
    BydaRadarHandle h, BydaReceiveDiagnostic* out);

/** Process received data: decode tracks, handle parameter responses.
 *  Returns the number of object tracks in the latest complete valid frame.
 *  Returns BYDA_ERR_NO_DATA when the TCP bytes do not yet contain a complete
 *  valid frame. In that case the last published scan and tracks are retained.
 *  Call byda_receive() first. */
BYDA_API int byda_process(BydaRadarHandle h);

/** Convenience: byda_receive() + byda_process() combined.
 *  Returns track count (>=0) only for a complete checksum-valid frame.
 *  A complete zero-track frame returns 0. A receive timeout or incomplete TCP
 *  fragment returns BYDA_ERR_NO_DATA; an orderly socket close or receive
 *  failure returns BYDA_ERR_RECV.
 *
 *  Legacy-BSR path only. On an HDR/4D radar the process() step is irrelevant -
 *  frames are assembled inside byda_receive() and delivered through the frame
 *  callback - so poll() may return 0/<0 while your frame callback is already
 *  firing. Use a plain byda_receive() loop for HDR instead. */
BYDA_API int byda_poll(BydaRadarHandle h);

/* ------------------------------------------------------------------ */
/* Track data accessors (valid after byda_process / byda_poll)        */
/* ------------------------------------------------------------------ */

/** Get scan metadata for the last complete checksum-valid frame.
 *  Returns BYDA_ERR_NO_DATA before the first such frame is received. */
BYDA_API int byda_get_scan_info(BydaRadarHandle h, BydaScanInfo* out);

/** Monotonic count of complete checksum-valid frames published by this handle.
 *  Returns 0 for a null handle or before the first complete frame. */
BYDA_API uint64_t byda_get_frame_generation(BydaRadarHandle h);

/** Get number of decoded object tracks. */
BYDA_API int byda_get_track_count(BydaRadarHandle h);

/** Get a single track by index [0 .. track_count-1]. */
BYDA_API int byda_get_track(BydaRadarHandle h, int index, BydaTrack* out);

/** Get all tracks at once. Caller provides array of at least max_count elements.
 *  Returns number of tracks written. */
BYDA_API int byda_get_tracks(BydaRadarHandle h, BydaTrack* out, int max_count);

/* ------------------------------------------------------------------ */
/* Parameter load (request from radar)                                */
/* ------------------------------------------------------------------ */

/** Request installation parameters from radar.
 *  Response arrives in subsequent process() cycles. */
BYDA_API int byda_load_install(BydaRadarHandle h);

/** Request lane parameters from radar. */
BYDA_API int byda_load_lane(BydaRadarHandle h);

/** Request detection line parameters from radar. */
BYDA_API int byda_load_detect(BydaRadarHandle h);

/** Request all detection ranges from the radar.
 *  commVer 0/1 sends one legacy LOAD. commVer 2/3 sequentially loads all four
 *  polygons. byda_is_param_idle() becomes true after the final reply. */
BYDA_API int byda_load_detection_ranges(BydaRadarHandle h);

/** Request product serial number from radar. */
BYDA_API int byda_load_serial(BydaRadarHandle h);

/** Request software version info from radar. */
BYDA_API int byda_load_sw_info(BydaRadarHandle h);

/** Check if parameter load is complete (loadState == Idle).
 *  Returns 1 if idle (ready for next load), 0 if still loading. */
BYDA_API int byda_is_param_idle(BydaRadarHandle h);

/** Force-reset parameter load state to Idle.
 *  Use when a load request times out (radar did not respond). */
BYDA_API void byda_reset_param_state(BydaRadarHandle h);

/* ------------------------------------------------------------------ */
/* Parameter data accessors (valid after load completes)              */
/* ------------------------------------------------------------------ */

/** Get installation info. Returns BYDA_ERR_NO_DATA if not yet loaded. */
BYDA_API int byda_get_install_info(BydaRadarHandle h, BydaInstallInfo* out);

/** Get total number of lanes configured. */
BYDA_API int byda_get_lane_count(BydaRadarHandle h);

/** Get lane info by index. Returns unified format regardless of protocol version. */
BYDA_API int byda_get_lane_info(BydaRadarHandle h, int index, BydaLaneInfo* out);

/** Get communication-v3 lane fields including curved-lane segments.
 *  Returns BYDA_ERR_UNSUPPORTED_VERSION for communication versions 0/1/2. */
BYDA_API int byda_get_lane_v3_info(
    BydaRadarHandle h, int index, BydaLaneV3Info* out);

/** Get detection line position (meters) — v1 single line. */
BYDA_API int byda_get_detect_line(BydaRadarHandle h, float* pos);

/** Get number of detection lines supported (1 for v0/v1, 5 for v2). */
BYDA_API int byda_get_detect_line_count(BydaRadarHandle h);

/** Get detection line info by index (v2/v3: 0..4, v0/v1: 0 only). */
BYDA_API int byda_get_detect_line_info(BydaRadarHandle h, int index, BydaDetectLineInfo* out);

/** Get supported detection-range count (1 for commVer 0/1, 4 for 2/3). */
BYDA_API int byda_get_detection_range_count(BydaRadarHandle h);

/** Get one cached detection range after byda_load_detection_ranges(). */
BYDA_API int byda_get_detection_range_info(
    BydaRadarHandle h, int index, BydaDetectionRangeInfo* out);

/** Get product serial info. */
BYDA_API int byda_get_serial_info(BydaRadarHandle h, BydaSerialInfo* out);

/** Get software version info. */
BYDA_API int byda_get_sw_info(BydaRadarHandle h, BydaSwInfo* out);

/** Request the radar to send its current radar-control parameters.
 *  Returns immediately; the reply is parsed asynchronously into the SDK
 *  cache. Poll byda_get_radar_control() afterwards. */
BYDA_API int byda_load_radar_control(BydaRadarHandle h);

/** Get cached radar-control parameters loaded from the radar.
 *  Returns BYDA_ERR_NO_DATA until byda_load_radar_control() completes. */
BYDA_API int byda_get_radar_control(BydaRadarHandle h, BydaRadarControlInfo* out);

/** Request the radar's network configuration (IP/Subnet/Gateway/Port/MAC).
 *  The reply is parsed asynchronously by the SDK; poll byda_is_param_idle()
 *  then call byda_get_radar_network(). */
BYDA_API int byda_load_radar_network(BydaRadarHandle h);

/** Get cached radar network configuration.
 *  Returns BYDA_ERR_NO_DATA until byda_load_radar_network() completes. */
BYDA_API int byda_get_radar_network(BydaRadarHandle h, BydaRadarNetworkInfo* out);

/* ------------------------------------------------------------------ */
/* Parameter set (write to radar)                                     */
/* ------------------------------------------------------------------ */

/** Set installation parameters on the radar. */
BYDA_API int byda_set_install(BydaRadarHandle h, float height, float distance,
                              float pitch, float azimuth, float speed_offset,
                              float road_angle, uint8_t direction);

/** Set the single detection-line position on a commVer 0/1 radar.
 *  Returns BYDA_ERR_UNSUPPORTED_VERSION for commVer 2/3; use
 *  byda_set_detect_line_info() + byda_save_detect_lines() there. */
BYDA_API int byda_set_detect_line(BydaRadarHandle h, float pos);

/** Stage one detection line into the SDK's internal buffer (does NOT send).
 *  Index range depends on the radar's protocol version:
 *    commVer 0/1 : only index 0 is valid (firmware stores 1 line).
 *    commVer 2/3 : index 0..4 (LOOP_NUM_MAX), all 5 slots committed together.
 *
 *  Call byda_save_detect_lines() once after staging to commit. Per-line packets
 *  are intentionally avoided because firmware v2/v3 SAVE overwrites flash with the
 *  full 5-slot payload of each packet, which would erase previously set lines.
 *  Unset v2/v3 slots remain En=0, Pos=0 (disabled). */
BYDA_API int byda_set_detect_line_info(BydaRadarHandle h, int index, const BydaDetectLineInfo* info);

/** Commit all staged detection lines to the radar in a single packet.
 *    commVer 0/1 : 16-byte legacy packet (password=0xFB80), single detectPos.
 *    commVer 2/3 : 20-byte packet (password=0xFB82), 5 detPos slots.
 *  Call after configuring lines with byda_set_detect_line_info(). */
BYDA_API int byda_save_detect_lines(BydaRadarHandle h);

/** Stage one detection range in the SDK cache without sending. */
BYDA_API int byda_set_detection_range_info(
    BydaRadarHandle h, int index, const BydaDetectionRangeInfo* info);

/** Save all staged detection ranges.
 *  commVer 0/1 sends one 28-byte packet (0xFB81).
 *  commVer 2/3 sends four 52-byte polygon packets (0xFB83). */
BYDA_API int byda_save_detection_ranges(BydaRadarHandle h);

/** Set a single lane configuration on the radar by index.
 *  index: lane index (0..5 for commVer 0/1, 0..12 for commVer 2/3).
 *  After setting individual lane(s), call byda_save_lanes() to
 *  commit all lanes to the radar in a single packet. */
BYDA_API int byda_set_lane(BydaRadarHandle h, int index, const BydaLaneInfo* info);

/** Stage one communication-v3 lane including curved-lane segments.
 *  Call byda_save_lanes() to commit the complete 1024-byte v3 lane packet.
 *  To modify only selected lanes, call byda_load_lane() first so all other
 *  lane and segment fields are preserved in the full-packet write.
 *  Returns BYDA_ERR_UNSUPPORTED_VERSION for communication versions 0/1/2. */
BYDA_API int byda_set_lane_v3(
    BydaRadarHandle h, int index, const BydaLaneV3Info* info);

/** Commit all lane settings to the radar.
 *  Builds the full lane packet (including header + checksum) and sends it.
 *  Call after configuring lanes with byda_set_lane(). */
BYDA_API int byda_save_lanes(BydaRadarHandle h);

/** Get the maximum number of lanes supported by the connected radar.
 *  Returns 6 for commVer 0/1, 13 for commVer 2/3. */
BYDA_API int byda_get_max_lanes(BydaRadarHandle h);

/** Send radar-control parameters to the radar (single 32-byte packet).
 *
 *  Recommended flow:
 *      BydaRadarControlInfo cur;
 *      byda_load_radar_control(h);          // wait for reply
 *      byda_get_radar_control(h, &cur);
 *      cur.func_enable |= BYDA_RC_FUNC_BIT(BYDA_RC_FUNC_DFS_OUT_FILTER);
 *      byda_set_radar_control(h, &cur);
 *
 *  SDK auto-fills ver = RADAR_CONTROL_PARAM_VERSION and enable = 1 so the
 *  firmware applies the packet. The factory-reset bit (init_SettingOn) is
 *  intentionally NOT exposed here — use byda_factory_reset_radar_control()
 *  if you really need it. */
BYDA_API int byda_set_radar_control(BydaRadarHandle h, const BydaRadarControlInfo* info);

/** Factory-reset all radar-control parameters back to firmware defaults.
 *  This sends a packet with init_SettingOn=1, which causes the radar to
 *  invoke its internal initRadarCtrlParam() routine. Other fields in the
 *  packet are ignored by the firmware in this mode. Use sparingly. */
BYDA_API int byda_factory_reset_radar_control(BydaRadarHandle h);

/** Change the radar device's IP/Subnet/Gateway/Port (66-byte 0xBA67 packet).
 *
 *  ★ Prerequisite: byda_load_radar_network() must have completed (poll
 *  byda_is_param_idle()). The SDK echoes model/serial/firmware/MAC fields
 *  from the LOAD cache; without a fresh LOAD this call returns BYDA_ERR_NO_DATA
 *  to prevent zero-overwriting those fields by accident.
 *
 *  ★ Only fields whose corresponding change_* flag is 1 are changed; others
 *  retain their LOAD-cache values. Regardless of commVer the SDK sends every
 *  network and MAC write flag enabled — the firmware erases un-flagged
 *  fields instead of preserving them. The MAC write flag stays 0 only when
 *  the LOAD cache holds an all-zero MAC, so the firmware restores its
 *  default MAC on reboot instead of persisting 00:00:00:00:00:00.
 *
 *  ★ The radar continues running on its old config until reboot. Call
 *  byda_reset() (or power-cycle) to activate the new settings.
 *
 *  ★ MAC is intentionally NOT writable via this API — use byda_set_radar_mac(). */
BYDA_API int byda_set_radar_network(BydaRadarHandle h, const BydaRadarNetworkInfo* info);

/** Change the radar device's MAC address (66-byte 0xBA67 packet).
 *  Same prerequisites as byda_set_radar_network(): LOAD must have completed.
 *  Reboot to activate. ★ MAC change is destructive and may make the device
 *  unreachable on the local network. */
BYDA_API int byda_set_radar_mac(BydaRadarHandle h, const uint8_t mac[6]);

/* ------------------------------------------------------------------ */
/* Relay / RD data structures                                         */
/* ------------------------------------------------------------------ */

/** Relay track data (slot 5 in relay-capable radar).
 *  Contains detection info for the relay channel. */
typedef struct {
    uint8_t  is_detect;    /* 1 if object detected, 0 otherwise    */
    uint8_t  obj_count;    /* number of detected objects            */
    float    range;        /* distance in meters  (raw / 10.0)     */
    float    velocity;     /* speed in kph        (raw / 10.0)     */
    uint8_t  power;        /* signal power                         */
} BydaRelayTrack;

/** RD-map track data (slots 6~65, 2 entries per slot = 1m resolution).
 *  Each entry represents a range-Doppler cell. */
typedef struct {
    int16_t  velocity;     /* speed in kph        (raw / 10.0)     */
    uint8_t  power;        /* signal power                         */
    uint8_t  reserved;     /* reserved for future use              */
} BydaRdTrack;

/* ------------------------------------------------------------------ */
/* Relay track callback (BSR20 relay-capable radar)                   */
/* ------------------------------------------------------------------ */

/** Relay track callback type.
 *  Called inside byda_process() when relay track data is available.
 *  Only fires when connected to a relay-capable radar (0x55AA handshake).
 *  'relay_track' points to a single BydaRelayTrack. */
typedef void (*byda_relay_cb_t)(BydaRadarHandle       handle,
                                const BydaScanInfo*    scan,
                                const BydaRelayTrack*  relay_track,
                                void*                  user_ctx);

/** Register relay track callback.  Pass NULL to unregister. */
BYDA_API int byda_set_relay_callback(BydaRadarHandle h,
                                     byda_relay_cb_t cb,
                                     void*           user_ctx);

/* ------------------------------------------------------------------ */
/* RD (Range-Doppler) track callback                                  */
/* ------------------------------------------------------------------ */

/** RD tracks callback type.
 *  Called inside byda_process() when RD track data is available.
 *  Only fires when connected to a relay-capable radar (0x55AA handshake).
 *  'rd_tracks' is an array of rd_count BydaRdTrack elements.
 *  Each slot contains 2 entries (1m resolution), total up to 120 entries. */
typedef void (*byda_rd_cb_t)(BydaRadarHandle     handle,
                             const BydaScanInfo*  scan,
                             const BydaRdTrack*   rd_tracks,
                             int                  rd_count,
                             void*                user_ctx);

/** Register RD tracks callback.  Pass NULL to unregister. */
BYDA_API int byda_set_rd_callback(BydaRadarHandle h,
                                  byda_rd_cb_t    cb,
                                  void*           user_ctx);

/* ------------------------------------------------------------------ */
/* Raw data callback (optional)                                       */
/* ------------------------------------------------------------------ */

/** Raw data callback type.
 *  Called inside byda_receive() with the raw TCP bytes just received.
 *
 *  Caveat: TCP is stream-oriented, so each invocation carries an arbitrary
 *  chunk of bytes. Radar frames are not aligned to callback boundaries. If you
 *  need complete packets per callback, use byda_set_packet_callback() instead. */
typedef void (*byda_raw_cb_t)(const uint8_t* data, int len, void* user_ctx);

/** Register a raw data callback.  Pass NULL to unregister.
 *  user_ctx is forwarded as-is to every callback invocation. */
BYDA_API int byda_set_raw_callback(BydaRadarHandle h, byda_raw_cb_t cb, void* user_ctx);

/* ------------------------------------------------------------------ */
/* Packet-level callback (complete reassembled BSR scan packets)      */
/* ------------------------------------------------------------------ */

/** Reassembled-packet callback type.
 *  Invoked from byda_process(), immediately after DataService extracts one
 *  complete scan packet and before track decoding/checksum filtering.
 *  Packet layout starts with the 8-byte sync pattern, followed by the
 *  16-byte commSendDataHeader and raw 8-byte trackOutElSt entries.
 *
 *  The buffer is owned by the SDK and is only valid for the duration of the
 *  callback. Copy it if you need to keep it.
 *
 *  Thread context: invoked synchronously on whichever thread is driving
 *  byda_receive()/byda_process(). */
typedef void (*byda_packet_cb_t)(const uint8_t* data, int len, void* user_ctx);

/** Register a packet-ready callback.  Pass NULL to unregister.
 *  Set this once before connect(); changing it while streaming is undefined. */
BYDA_API int byda_set_packet_callback(BydaRadarHandle h, byda_packet_cb_t cb, void* user_ctx);

/* ------------------------------------------------------------------ */
/* Legacy combined-SDK compatibility                                  */
/* ------------------------------------------------------------------ */

/** Deprecated HDR symbols retained so existing BSR applications can load
 *  this DLL unchanged. New HDR applications must use Hdr10Sdk.h. */
typedef void* BydaFrameHandle;
typedef void (*byda_error_cb_t)(const char* reason, void* user_ctx);
typedef void (*byda_frame_cb_t)(BydaFrameHandle frame, void* user_ctx);

/** BSR compatibility registrations. These callbacks do not fire for BSR data. */
BYDA_API int byda_set_error_callback(
    BydaRadarHandle h, byda_error_cb_t cb, void* user_ctx);
BYDA_API int byda_set_frame_callback(
    BydaRadarHandle h, byda_frame_cb_t cb, void* user_ctx);

/** Deprecated aliases for the corresponding byda_hdr10_frame_* accessors. */
BYDA_API uint32_t byda_frame_get_num(BydaFrameHandle f);
BYDA_API int byda_frame_get_dyn_high_count(BydaFrameHandle f);
BYDA_API int byda_frame_get_dyn_low_count(BydaFrameHandle f);
BYDA_API int byda_frame_get_mm_long_high_count(BydaFrameHandle f);
BYDA_API int byda_frame_get_mm_long_low_count(BydaFrameHandle f);
BYDA_API int byda_frame_get_mm_short_high_count(BydaFrameHandle f);
BYDA_API int byda_frame_get_mm_short_low_count(BydaFrameHandle f);
BYDA_API int byda_frame_get_track_count(BydaFrameHandle f);
BYDA_API int byda_frame_get_dyn_high_point(
    BydaFrameHandle f, int idx,
    float* x_m, float* y_m, float* z_m, float* vel_ms, uint16_t* snr);
BYDA_API int byda_frame_get_track_point(
    BydaFrameHandle f, int idx, float* x_m, float* y_m, float* z_m);
BYDA_API int byda_frame_has_sysinfo(BydaFrameHandle f);
BYDA_API int byda_frame_get_serial_info(BydaFrameHandle f, BydaSerialInfo* out);
BYDA_API int byda_frame_get_sw_info(BydaFrameHandle f, BydaSwInfo* out);
BYDA_API int byda_frame_get_network_info(
    BydaFrameHandle f, BydaRadarNetworkInfo* out);

#ifdef __cplusplus
}
#endif

#endif /* BYDA_C_API_H */
