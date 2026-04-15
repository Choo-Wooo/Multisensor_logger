/*=============================================================================
 * BYDA Radar SDK - C ABI Interface
 *
 * C ABI wrapper for the BYDA radar SDK.
 * Build as shared library (.so / .dll) and use from any language via FFI.
 *
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

/* ------------------------------------------------------------------ */
/* C-compatible data structures (all values in physical units)        */
/* ------------------------------------------------------------------ */
#pragma pack(push, 4)

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

/** Detection line info. */
typedef struct {
    uint8_t  enable;       /* 0=disabled, 1=enabled */
    float    position;     /* detection line position in meters */
} BydaDetectLineInfo;

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

/** Get communication protocol version (available after connect). */
BYDA_API int byda_get_comm_version(BydaRadarHandle h);

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

/** Send system reset command to radar. */
BYDA_API int byda_reset(BydaRadarHandle h);

/* ------------------------------------------------------------------ */
/* Data reception loop                                                */
/* ------------------------------------------------------------------ */

/** Receive raw data from radar (blocking TCP recv).
 *  Returns bytes received (>0), 0 if connection closed, <0 on error. */
BYDA_API int byda_receive(BydaRadarHandle h);

/** Process received data: decode tracks, handle parameter responses.
 *  Returns number of object tracks decoded, or <0 on error.
 *  Call byda_receive() first. */
BYDA_API int byda_process(BydaRadarHandle h);

/** Convenience: byda_receive() + byda_process() combined.
 *  Returns track count (>=0), or <0 on error/no data. */
BYDA_API int byda_poll(BydaRadarHandle h);

/* ------------------------------------------------------------------ */
/* Track data accessors (valid after byda_process / byda_poll)        */
/* ------------------------------------------------------------------ */

/** Get scan metadata for the last processed frame. */
BYDA_API int byda_get_scan_info(BydaRadarHandle h, BydaScanInfo* out);

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

/** Get detection line position (meters) — v1 single line. */
BYDA_API int byda_get_detect_line(BydaRadarHandle h, float* pos);

/** Get number of detection lines supported (1 for v0/v1, 5 for v2). */
BYDA_API int byda_get_detect_line_count(BydaRadarHandle h);

/** Get detection line info by index (v2: 0..4, v0/v1: 0 only). */
BYDA_API int byda_get_detect_line_info(BydaRadarHandle h, int index, BydaDetectLineInfo* out);

/** Get product serial info. */
BYDA_API int byda_get_serial_info(BydaRadarHandle h, BydaSerialInfo* out);

/** Get software version info. */
BYDA_API int byda_get_sw_info(BydaRadarHandle h, BydaSwInfo* out);

/* ------------------------------------------------------------------ */
/* Parameter set (write to radar)                                     */
/* ------------------------------------------------------------------ */

/** Set installation parameters on the radar. */
BYDA_API int byda_set_install(BydaRadarHandle h, float height, float distance,
                              float pitch, float azimuth, float speed_offset,
                              float road_angle, uint8_t direction);

/** Set detection line position on the radar (v1 single line). */
BYDA_API int byda_set_detect_line(BydaRadarHandle h, float pos);

/** Set detection line by index (v2: 0..4).
 *  After setting, the value is sent to radar immediately. */
BYDA_API int byda_set_detect_line_info(BydaRadarHandle h, int index, const BydaDetectLineInfo* info);

/** Set a single lane configuration on the radar by index.
 *  index: lane index (0..12 for commVer>=2, 0..5 for v0/v1).
 *  After setting individual lane(s), call byda_save_lanes() to
 *  commit all lanes to the radar in a single packet. */
BYDA_API int byda_set_lane(BydaRadarHandle h, int index, const BydaLaneInfo* info);

/** Commit all lane settings to the radar.
 *  Builds the full lane packet (including header + checksum) and sends it.
 *  Call after configuring lanes with byda_set_lane(). */
BYDA_API int byda_save_lanes(BydaRadarHandle h);

/** Get the maximum number of lanes supported by the connected radar.
 *  Returns 6 for commVer 0/1, 13 for commVer 2+. */
BYDA_API int byda_get_max_lanes(BydaRadarHandle h);

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
 *  Use this to feed bytes into a custom parser (e.g. SensorParser). */
typedef void (*byda_raw_cb_t)(const uint8_t* data, int len, void* user_ctx);

/** Register a raw data callback.  Pass NULL to unregister.
 *  user_ctx is forwarded as-is to every callback invocation. */
BYDA_API int byda_set_raw_callback(BydaRadarHandle h, byda_raw_cb_t cb, void* user_ctx);

/* ------------------------------------------------------------------ */
/* Frame-level callback (opaque handle – no internal headers exposed) */
/* ------------------------------------------------------------------ */

/** Opaque handle to a parsed radar frame.
 *  Valid only for the duration of the byda_frame_cb_t callback. */
typedef void* BydaFrameHandle;

/** Called when the SDK has assembled a complete 4D radar frame.
 *  Invoked on the receive thread (same thread as byda_receive()).
 *  Use byda_frame_get_*() accessors to read data from 'frame'. */
typedef void (*byda_frame_cb_t)(BydaFrameHandle frame, void* user_ctx);

/** Register a frame-ready callback.
 *  The SDK maintains an internal parser; raw TCP bytes received by
 *  byda_receive() are automatically assembled into frames.
 *  Pass NULL to unregister. */
BYDA_API int byda_set_frame_callback(BydaRadarHandle h,
                                     byda_frame_cb_t  cb,
                                     void*            user_ctx);

/* ------------------------------------------------------------------ */
/* Frame accessors (valid only inside the frame callback)             */
/* ------------------------------------------------------------------ */

/** Frame sequence number. */
BYDA_API uint32_t byda_frame_get_num(BydaFrameHandle f);

/** Number of dynamic-high point cloud objects. */
BYDA_API int byda_frame_get_dyn_high_count(BydaFrameHandle f);

/** Number of dynamic-low point cloud objects. */
BYDA_API int byda_frame_get_dyn_low_count(BydaFrameHandle f);

/** Number of mm-wave long-range high point cloud objects. */
BYDA_API int byda_frame_get_mm_long_high_count(BydaFrameHandle f);

/** Number of mm-wave long-range low point cloud objects. */
BYDA_API int byda_frame_get_mm_long_low_count(BydaFrameHandle f);

/** Number of mm-wave short-range high point cloud objects. */
BYDA_API int byda_frame_get_mm_short_high_count(BydaFrameHandle f);

/** Number of mm-wave short-range low point cloud objects. */
BYDA_API int byda_frame_get_mm_short_low_count(BydaFrameHandle f);

/** Number of track points. */
BYDA_API int byda_frame_get_track_count(BydaFrameHandle f);

/** Get a single dynamic-high point by index.
 *  x_m / y_m / z_m: position in meters.  vel_ms: radial velocity m/s.
 *  Returns BYDA_OK or BYDA_ERR_INVALID_INDEX. */
BYDA_API int byda_frame_get_dyn_high_point(BydaFrameHandle f, int idx,
                                           float* x_m, float* y_m,
                                           float* z_m, float* vel_ms,
                                           uint16_t* snr);

/** Get a single track point by index.
 *  x_m / y_m / z_m: position in meters.
 *  Returns BYDA_OK or BYDA_ERR_INVALID_INDEX. */
BYDA_API int byda_frame_get_track_point(BydaFrameHandle f, int idx,
                                        float* x_m, float* y_m, float* z_m);

#ifdef __cplusplus
}
#endif

#endif /* BYDA_C_API_H */
