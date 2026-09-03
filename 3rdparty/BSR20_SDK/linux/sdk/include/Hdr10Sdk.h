/*=============================================================================
 * BYDA HDR10 Radar SDK - C ABI Interface
 *
 * HDR10 uses a push model: register callbacks, connect, then drive
 * byda_hdr10_receive() from one owning thread. The frame handle passed to a
 * callback is valid only until that callback returns.
 *=============================================================================*/

#ifndef BYDA_HDR10_C_API_H
#define BYDA_HDR10_C_API_H

#include "Bsr20Sdk.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void* BydaHdr10Handle;
typedef void* BydaHdr10FrameHandle;

typedef void (*byda_hdr10_raw_cb_t)(const uint8_t* data, int len, void* user_ctx);
typedef void (*byda_hdr10_error_cb_t)(const char* reason, void* user_ctx);
typedef void (*byda_hdr10_packet_cb_t)(const uint8_t* data, int len, void* user_ctx);
typedef void (*byda_hdr10_frame_cb_t)(BydaHdr10FrameHandle frame, void* user_ctx);

#define BYDA_HDR10_CONFIG_MAX_CAPABILITIES 32
#define BYDA_HDR10_CONFIG_MAX_RESPONSE_ITEMS 48
#define BYDA_HDR10_CONFIG_GENERATION_ANY 0xFFFFFFFFU
#define BYDA_HDR10_CONFIG_MAX_ITEMS 32
#define BYDA_HDR10_CONFIG_ITEM_MAX_BYTES 16

#define BYDA_HDR10_CONFIG_FLAG_PERSIST       0x0001U
#define BYDA_HDR10_CONFIG_FLAG_APPLY_NOW     0x0002U
#define BYDA_HDR10_CONFIG_FLAG_RETURN_VALUES 0x0004U

#define BYDA_HDR10_CONFIG_RESPONSE_PERSISTED        0x0001U
#define BYDA_HDR10_CONFIG_RESPONSE_APPLIED          0x0002U
#define BYDA_HDR10_CONFIG_RESPONSE_REBOOT_REQUIRED  0x0004U
#define BYDA_HDR10_CONFIG_RESPONSE_VALUES_TRUNCATED 0x0008U

#define BYDA_HDR10_CONFIG_ACCESS_READABLE        0x01U
#define BYDA_HDR10_CONFIG_ACCESS_WRITABLE        0x02U
#define BYDA_HDR10_CONFIG_ACCESS_APPLY_NOW       0x04U
#define BYDA_HDR10_CONFIG_ACCESS_PERSIST         0x08U
#define BYDA_HDR10_CONFIG_ACCESS_REBOOT_REQUIRED 0x10U

#define BYDA_HDR10_CONFIG_STATUS_OK                    0x0000U
#define BYDA_HDR10_CONFIG_STATUS_MALFORMED_FRAME       0x0101U
#define BYDA_HDR10_CONFIG_STATUS_CRC_MISMATCH          0x0102U
#define BYDA_HDR10_CONFIG_STATUS_UNSUPPORTED_VERSION   0x0103U
#define BYDA_HDR10_CONFIG_STATUS_UNSUPPORTED_MESSAGE   0x0104U
#define BYDA_HDR10_CONFIG_STATUS_INVALID_FLAGS         0x0105U
#define BYDA_HDR10_CONFIG_STATUS_MALFORMED_PAYLOAD     0x0201U
#define BYDA_HDR10_CONFIG_STATUS_UNKNOWN_FIELD         0x0202U
#define BYDA_HDR10_CONFIG_STATUS_TYPE_MISMATCH         0x0203U
#define BYDA_HDR10_CONFIG_STATUS_OUT_OF_RANGE          0x0204U
#define BYDA_HDR10_CONFIG_STATUS_CONSTRAINT_VIOLATION  0x0205U
#define BYDA_HDR10_CONFIG_STATUS_DUPLICATE_FIELD       0x0206U
#define BYDA_HDR10_CONFIG_STATUS_READ_ONLY             0x0207U
#define BYDA_HDR10_CONFIG_STATUS_STALE_GENERATION      0x0208U
#define BYDA_HDR10_CONFIG_STATUS_APPLY_FAILED           0x0301U
#define BYDA_HDR10_CONFIG_STATUS_PERSIST_FAILED         0x0302U
#define BYDA_HDR10_CONFIG_STATUS_PERSIST_NOT_SUPPORTED  0x0303U
#define BYDA_HDR10_CONFIG_STATUS_BUSY                   0x0304U
#define BYDA_HDR10_CONFIG_STATUS_INTERNAL_ERROR         0x0305U

#define BYDA_HDR10_CONFIG_VALUE_NONE  0x00U
#define BYDA_HDR10_CONFIG_VALUE_BOOL  0x01U
#define BYDA_HDR10_CONFIG_VALUE_U8    0x02U
#define BYDA_HDR10_CONFIG_VALUE_U16   0x03U
#define BYDA_HDR10_CONFIG_VALUE_U32   0x04U
#define BYDA_HDR10_CONFIG_VALUE_I32   0x05U
#define BYDA_HDR10_CONFIG_VALUE_BYTES 0x06U

#define BYDA_HDR10_FIELD_NETWORK_IPV4             0x0101U
#define BYDA_HDR10_FIELD_NETWORK_SUBNET           0x0102U
#define BYDA_HDR10_FIELD_NETWORK_GATEWAY          0x0103U
#define BYDA_HDR10_FIELD_NETWORK_TCP_PORT         0x0104U
#define BYDA_HDR10_FIELD_NETWORK_MAC              0x0105U
#define BYDA_HDR10_FIELD_RADAR_HEIGHT_MM          0x0201U
#define BYDA_HDR10_FIELD_RADAR_INCLINATION_MDEG   0x0202U
#define BYDA_HDR10_FIELD_RADAR_MOUNTING_TYPE      0x0203U
#define BYDA_HDR10_FIELD_CFAR_DYNAMIC_X1000       0x0301U
#define BYDA_HDR10_FIELD_CFAR_SHORT_MMS_X1000     0x0302U
#define BYDA_HDR10_FIELD_CFAR_LONG_MMS_X1000      0x0303U
#define BYDA_HDR10_FIELD_MODEL_SERIES_ID          0x0401U
#define BYDA_HDR10_FIELD_MODEL_PRODUCT_ID         0x0402U
#define BYDA_HDR10_FIELD_MODEL_GENERATION_ID      0x0403U
#define BYDA_HDR10_FIELD_MODEL_SENSOR_ID          0x0404U
#define BYDA_HDR10_FIELD_MODEL_ELECTRICAL_VERSION 0x0405U
#define BYDA_HDR10_FIELD_MODEL_MECHANICAL_VERSION 0x0406U
#define BYDA_HDR10_FIELD_SERIAL_PRODUCT_STATUS    0x0410U
#define BYDA_HDR10_FIELD_SERIAL_YEAR              0x0411U
#define BYDA_HDR10_FIELD_SERIAL_MONTH             0x0412U
#define BYDA_HDR10_FIELD_SERIAL_DAY               0x0413U
#define BYDA_HDR10_FIELD_SERIAL_NUMBER            0x0414U
#define BYDA_HDR10_FIELD_CUSTOMER_ID              0x0420U
#define BYDA_HDR10_FIELD_LICENSE_ID               0x0421U
#define BYDA_HDR10_FIELD_BOOT_VERSION             0x0430U
#define BYDA_HDR10_FIELD_KERNEL_VERSION           0x0431U
#define BYDA_HDR10_FIELD_FIRMWARE_VERSION         0x0432U
#define BYDA_HDR10_FIELD_SP_LIBRARY_VERSION       0x0433U
#define BYDA_HDR10_FIELD_BOUNDARY_AX_MM            0x0501U
#define BYDA_HDR10_FIELD_BOUNDARY_AY_MM            0x0502U
#define BYDA_HDR10_FIELD_BOUNDARY_BX_MM            0x0503U
#define BYDA_HDR10_FIELD_BOUNDARY_BY_MM            0x0504U
#define BYDA_HDR10_FIELD_BOUNDARY_CX_MM            0x0505U
#define BYDA_HDR10_FIELD_BOUNDARY_CY_MM            0x0506U
#define BYDA_HDR10_FIELD_BOUNDARY_DX_MM            0x0507U
#define BYDA_HDR10_FIELD_BOUNDARY_DY_MM            0x0508U
#define BYDA_HDR10_FIELD_BOUNDARY_LOWER_Z_MM       0x0509U
#define BYDA_HDR10_FIELD_BOUNDARY_UPPER_Z_MM       0x050AU
#define BYDA_HDR10_FIELD_TRACK_DET2ACT_COUNT       0x0601U
#define BYDA_HDR10_FIELD_TRACK_OBJECT_SNR_THRESHOLD 0x0602U
#define BYDA_HDR10_FIELD_VITAL_FIXED_BIN           0x0701U
/* Point Diagnostics counters are read-only and reset when the radar reboots. */
#define BYDA_HDR10_FIELD_POINT_DIAG_FRAME_COUNT     0x0801U
#define BYDA_HDR10_FIELD_POINT_DIAG_POINT_COUNT     0x0802U
#define BYDA_HDR10_FIELD_POINT_DIAG_GRADE_ZERO_COUNT 0x0803U
#define BYDA_HDR10_FIELD_POINT_DIAG_SNR_ZERO_COUNT  0x0804U
#define BYDA_HDR10_FIELD_POINT_DIAG_RANGE_ZERO_COUNT 0x0805U
#define BYDA_HDR10_FIELD_POINT_DIAG_ORIGIN_POINT_COUNT 0x0806U

#define BYDA_HDR10_MOUNTING_WALL    0U
#define BYDA_HDR10_MOUNTING_CEILING 1U

#pragma pack(push, 4)

typedef struct {
    uint16_t field_id;
    uint8_t value_type;
    uint8_t access_flags;
    uint16_t value_length;
} BydaHdr10ConfigCapability;

typedef struct {
    uint8_t minimum_version;
    uint8_t maximum_version;
    uint16_t maximum_frame_length;
    uint16_t maximum_items;
    uint16_t count;
    BydaHdr10ConfigCapability fields[BYDA_HDR10_CONFIG_MAX_CAPABILITIES];
} BydaHdr10ConfigCapabilities;

/** Scalar metadata returned with an extended capability query.
 *
 *  The descriptor array is supplied separately by the caller so firmware
 *  profiles can advertise more than the legacy fixed-array limit of 32
 *  fields without changing this structure's ABI. */
typedef struct {
    uint8_t minimum_version;
    uint8_t maximum_version;
    uint16_t maximum_frame_length;
    uint16_t maximum_items;
} BydaHdr10ConfigCapabilitiesInfo;

typedef struct {
    uint16_t status;
    uint16_t failed_field_id;
    uint32_t generation;
    uint16_t response_flags;
} BydaHdr10ConfigResult;

typedef struct {
    uint8_t ip[4];
    uint8_t subnet[4];
    uint8_t gateway[4];
    uint16_t port;
    uint8_t mac[6];
} BydaHdr10NetworkConfig;

typedef struct {
    uint32_t height_mm;
    int32_t inclination_mdeg;
    uint8_t mounting_type;
} BydaHdr10InstallationConfig;

typedef struct {
    uint32_t dynamic_x1000;
    uint32_t short_mms_x1000;
    uint32_t long_mms_x1000;
} BydaHdr10CfarConfig;

/** Device identity. Read it with byda_hdr10_config_get() and write individual
 *  fields with byda_hdr10_config_set_items(). Boot/kernel/firmware/SP library
 *  versions are read-only on the firmware side.
 *
 *  A GET may report an unset manufacturing month or day as 0xFF, which is
 *  outside the range a SET accepts. Never echo a GET result back field by
 *  field; send only what the operator actually changed. */
typedef struct {
    uint8_t model_series_id;
    uint8_t model_product_id;
    uint8_t model_generation_id;
    uint8_t model_sensor_id;
    uint8_t model_electrical_version;
    uint8_t model_mechanical_version;
    uint8_t serial_product_status;
    uint8_t serial_year;
    uint8_t serial_month;
    uint8_t serial_day;
    uint16_t serial_number;
    uint16_t customer_id;
    uint16_t license_id;
    uint8_t boot_version;
    uint8_t kernel_version;
    uint8_t firmware_version;
    uint8_t sp_library_version;
} BydaHdr10DeviceIdentity;

typedef struct {
    uint32_t generation;
    BydaHdr10NetworkConfig network;
    BydaHdr10InstallationConfig installation;
    BydaHdr10CfarConfig cfar;
    BydaHdr10DeviceIdentity identity;
} BydaHdr10Config;

/** One field of a byda_hdr10_config_set_items() request.
 *
 *  field_id is a BYDA_HDR10_FIELD_* constant and value_type the matching
 *  BYDA_HDR10_CONFIG_VALUE_* constant. BOOL/U8/U16/U32 values go in scalar;
 *  an I32 value goes in scalar as its two's-complement bit pattern. BYTES
 *  values go in bytes with byte_length set; the other members are ignored. */
typedef struct {
    uint16_t field_id;
    uint8_t value_type;
    uint8_t byte_length;
    uint32_t scalar;
    uint8_t bytes[BYDA_HDR10_CONFIG_ITEM_MAX_BYTES];
} BydaHdr10ConfigItem;

#pragma pack(pop)

BYDA_API BydaHdr10Handle byda_hdr10_create(void);
BYDA_API void byda_hdr10_destroy(BydaHdr10Handle h);

BYDA_API int byda_hdr10_set_address(BydaHdr10Handle h, const char* ip, int port);
BYDA_API int byda_hdr10_connect(BydaHdr10Handle h);
BYDA_API void byda_hdr10_disconnect(BydaHdr10Handle h);
BYDA_API int byda_hdr10_reset(BydaHdr10Handle h);

/** Receive and dispatch HDR10 stream data.
 *
 *  The call waits for at most 500 ms. A timeout returns 0 and is not a
 *  disconnect; call again to continue receiving. Negative values report
 *  connection or argument errors. */
BYDA_API int byda_hdr10_receive(BydaHdr10Handle h);

/** Get the outcome of the most recent socket receive operation. */
BYDA_API int byda_hdr10_get_last_receive_diagnostic(
    BydaHdr10Handle h, BydaReceiveDiagnostic* out);

BYDA_API int byda_hdr10_set_raw_callback(
    BydaHdr10Handle h, byda_hdr10_raw_cb_t cb, void* user_ctx);
BYDA_API int byda_hdr10_set_error_callback(
    BydaHdr10Handle h, byda_hdr10_error_cb_t cb, void* user_ctx);
BYDA_API int byda_hdr10_set_packet_callback(
    BydaHdr10Handle h, byda_hdr10_packet_cb_t cb, void* user_ctx);
BYDA_API int byda_hdr10_set_frame_callback(
    BydaHdr10Handle h, byda_hdr10_frame_cb_t cb, void* user_ctx);

/** Synchronously query the firmware Config V2 capability descriptors.
 *
 *  This call owns socket receive processing until the matching response is
 *  received. Radar frames received while waiting are still delivered through
 *  the registered callbacks. Do not call concurrently with byda_hdr10_receive().
 */
BYDA_API int byda_hdr10_config_get_capabilities(
    BydaHdr10Handle h, BydaHdr10ConfigCapabilities* out);

/** Query capability metadata and descriptors into caller-provided storage.
 *
 *  field_capacity is the number of elements available in fields. On success
 *  field_count receives the number written. Allocate
 *  BYDA_HDR10_CONFIG_MAX_RESPONSE_ITEMS descriptors for every current
 *  firmware profile. */
BYDA_API int byda_hdr10_config_get_capabilities_ex(
    BydaHdr10Handle h,
    BydaHdr10ConfigCapabilitiesInfo* info,
    BydaHdr10ConfigCapability* fields,
    int field_capacity,
    int* field_count);

/** Synchronously read all Config V2 fields.
 *
 *  A valid protocol response returns BYDA_OK even when result->status reports
 *  a firmware-side error.
 */
BYDA_API int byda_hdr10_config_get(
    BydaHdr10Handle h, BydaHdr10Config* out, BydaHdr10ConfigResult* result);

/** Read every Config V2 TLV into caller-provided storage.
 *
 *  item_capacity is the number of elements available in items. On success
 *  item_count receives the number written. Vehicle and Human Vital profiles
 *  currently return 46 and 47 items respectively, so allocate
 *  BYDA_HDR10_CONFIG_MAX_RESPONSE_ITEMS elements.
 *
 *  Presence is profile-dependent. In particular, VITAL_FIXED_BIN is returned
 *  only when advertised by capabilities. */
BYDA_API int byda_hdr10_config_get_items(
    BydaHdr10Handle h,
    BydaHdr10ConfigItem* items,
    int item_capacity,
    int* item_count,
    BydaHdr10ConfigResult* result);

/** Write an arbitrary subset of Config V2 fields in one request.
 *
 *  Send only the fields the operator actually changed. The firmware applies a
 *  SET all-or-nothing, so re-sending untouched values can fail the whole
 *  request on a read-only field or on a GET-only sentinel such as an unset
 *  manufacturing month or day. This is the API to use for device identity and
 *  for any partial installation/CFAR update.
 *
 *  flags is any combination of BYDA_HDR10_CONFIG_FLAG_*. item_count must be
 *  between 1 and BYDA_HDR10_CONFIG_MAX_ITEMS.
 *
 *  Writability is not checked locally. Read byda_hdr10_config_get_capabilities()
 *  and drop fields lacking BYDA_HDR10_CONFIG_ACCESS_WRITABLE first, otherwise
 *  the firmware rejects the request with BYDA_HDR10_CONFIG_STATUS_READ_ONLY and
 *  names the field in result->failed_field_id.
 */
BYDA_API int byda_hdr10_config_set_items(
    BydaHdr10Handle h,
    const BydaHdr10ConfigItem* items,
    int item_count,
    uint32_t expected_generation,
    uint16_t flags,
    BydaHdr10ConfigResult* result);

/** Write a subset and return effective TLVs from the SET response.
 *
 *  effective_capacity must be at least item_count so capacity errors are
 *  detected before a mutating request is sent. effective_count receives the
 *  number of response items, which may be zero unless RETURN_VALUES is set.
 */
BYDA_API int byda_hdr10_config_set_items_ex(
    BydaHdr10Handle h,
    const BydaHdr10ConfigItem* items,
    int item_count,
    uint32_t expected_generation,
    uint16_t flags,
    BydaHdr10ConfigResult* result,
    BydaHdr10ConfigItem* effective_items,
    int effective_capacity,
    int* effective_count);

/** Persist IPv4/Subnet/Gateway/TCP port as one atomic network update.
 *  The firmware reports REBOOT_REQUIRED on success. MAC is set separately. */
BYDA_API int byda_hdr10_config_set_network(
    BydaHdr10Handle h,
    const BydaHdr10NetworkConfig* config,
    uint32_t expected_generation,
    BydaHdr10ConfigResult* result);

/** Persist a MAC address in normal display order (first octet first).
 *  The firmware reports REBOOT_REQUIRED on success. */
BYDA_API int byda_hdr10_config_set_mac(
    BydaHdr10Handle h,
    const uint8_t mac[6],
    uint32_t expected_generation,
    BydaHdr10ConfigResult* result);

/** Apply installation fields immediately and optionally persist them.
 *  Pass persist=1 to retain values across reboot, or 0 for runtime-only. */
BYDA_API int byda_hdr10_config_set_installation(
    BydaHdr10Handle h,
    const BydaHdr10InstallationConfig* config,
    uint32_t expected_generation,
    uint8_t persist,
    BydaHdr10ConfigResult* result);

/** Apply CFAR fields immediately and optionally persist them.
 *  Pass persist=1 to retain values across reboot, or 0 for runtime-only. */
BYDA_API int byda_hdr10_config_set_cfar(
    BydaHdr10Handle h,
    const BydaHdr10CfarConfig* config,
    uint32_t expected_generation,
    uint8_t persist,
    BydaHdr10ConfigResult* result);

BYDA_API uint32_t byda_hdr10_frame_get_num(BydaHdr10FrameHandle f);
BYDA_API int byda_hdr10_frame_get_dyn_high_count(BydaHdr10FrameHandle f);
BYDA_API int byda_hdr10_frame_get_dyn_low_count(BydaHdr10FrameHandle f);
BYDA_API int byda_hdr10_frame_get_mm_long_high_count(BydaHdr10FrameHandle f);
BYDA_API int byda_hdr10_frame_get_mm_long_low_count(BydaHdr10FrameHandle f);
BYDA_API int byda_hdr10_frame_get_mm_short_high_count(BydaHdr10FrameHandle f);
BYDA_API int byda_hdr10_frame_get_mm_short_low_count(BydaHdr10FrameHandle f);
BYDA_API int byda_hdr10_frame_get_track_count(BydaHdr10FrameHandle f);

BYDA_API int byda_hdr10_frame_get_dyn_high_point(
    BydaHdr10FrameHandle f, int idx,
    float* x_m, float* y_m, float* z_m, float* vel_ms, uint16_t* snr);
BYDA_API int byda_hdr10_frame_get_dyn_low_point(
    BydaHdr10FrameHandle f, int idx,
    float* x_m, float* y_m, float* z_m, float* vel_ms, uint16_t* snr);
BYDA_API int byda_hdr10_frame_get_mm_long_high_point(
    BydaHdr10FrameHandle f, int idx,
    float* x_m, float* y_m, float* z_m, float* vel_ms, uint16_t* snr);
BYDA_API int byda_hdr10_frame_get_mm_long_low_point(
    BydaHdr10FrameHandle f, int idx,
    float* x_m, float* y_m, float* z_m, float* vel_ms, uint16_t* snr);
BYDA_API int byda_hdr10_frame_get_mm_short_high_point(
    BydaHdr10FrameHandle f, int idx,
    float* x_m, float* y_m, float* z_m, float* vel_ms, uint16_t* snr);
BYDA_API int byda_hdr10_frame_get_mm_short_low_point(
    BydaHdr10FrameHandle f, int idx,
    float* x_m, float* y_m, float* z_m, float* vel_ms, uint16_t* snr);
BYDA_API int byda_hdr10_frame_get_track_point(
    BydaHdr10FrameHandle f, int idx, float* x_m, float* y_m, float* z_m);

BYDA_API int byda_hdr10_frame_has_sysinfo(BydaHdr10FrameHandle f);
BYDA_API int byda_hdr10_frame_get_serial_info(
    BydaHdr10FrameHandle f, BydaSerialInfo* out);
BYDA_API int byda_hdr10_frame_get_sw_info(
    BydaHdr10FrameHandle f, BydaSwInfo* out);
BYDA_API int byda_hdr10_frame_get_network_info(
    BydaHdr10FrameHandle f, BydaRadarNetworkInfo* out);

#ifdef __cplusplus
}
#endif

#endif /* BYDA_HDR10_C_API_H */
