#pragma once

#ifdef _WIN32
  #ifdef BSR30_SDK_EXPORTS
    #define BSR30_API __declspec(dllexport)
  #else
    #define BSR30_API __declspec(dllimport)
  #endif
#else
  #define BSR30_API __attribute__((visibility("default")))
#endif

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// 레이더 데이터 구조체
#define BSR30_TRACK_COUNT 1024

#ifdef _MSC_VER
  #pragma pack(push, 1)
#endif

// 자료구조 - 트랙
typedef struct
#ifndef _MSC_VER
__attribute__((packed))
#endif
{
    uint8_t  id;                  // 트랙 ID
    uint8_t  _reserved0;
    uint16_t pw;                  // 신호 세기 (Power)
    uint32_t spFlag;              // SP Flag
    float    angle_deg;           // 각도 (deg)
    float    initPosVY_kph;       // 초기 Y속도 (kph)
    float    xPos_pred_m;         // X 위치 예측값 (m)
    float    yPos_pred_m;         // Y 위치 예측값 (m)
    float    xVel_pred_kph;       // X 속도 예측값 (kph)
    float    yVel_pred_kph;       // Y 속도 예측값 (kph)
    int8_t   laneNum;             // 현재 차선 번호
    uint8_t  vehicleType;         // 차량 타입 (sp_tracking 모드)
    uint8_t  ab_flag;             // AntVel AB-F 플래그
    int8_t   initLaneNum;         // 초기 차선 번호
    uint8_t  _padding[4];
} bsr30_track_t;

#ifdef _MSC_VER
  #pragma pack(pop)
#endif

// 자료구조 - 트랙 프레임
typedef struct {
    uint16_t      sequence;     // 프레임 시퀀스 번호
    uint32_t      timestamp;    // 타임스탬프 (ms)
    uint32_t      sys_frame_num; // 시스템 프레임 번호
    bsr30_track_t tracks[BSR30_TRACK_COUNT]; // 트랙 정보 배열
} bsr30_frame_t;

// API - 트랙 프레임 수신
typedef void (*bsr30_frame_cb)(const bsr30_frame_t* frame);

// 자료구조 - SDK 버전 정보
typedef struct {
    int         major;        // 메이저 버전
    int         minor;        // 마이너 버전
    int         patch;        // 패치 버전
    const char* name;         // SDK 이름
    const char* manufacturer; // 제조사
} bsr30_sdk_version_t;

// API - SDK 버전 정보 조회
BSR30_API void bsr30_sdk_get_version(bsr30_sdk_version_t* version);

// API - 레이더 연결
BSR30_API bool bsr30_connect(const char* radarIp, int tcpPort, int udpPort);

// API - 레이더 연결 해제
BSR30_API void bsr30_disconnect();

// API - 레이더 데이터 스트리밍 시작
BSR30_API bool bsr30_radar_start();

// API -레이더 데이터 스트리밍 중지
BSR30_API bool bsr30_radar_stop();

// API -레이더 프레임 콜백 등록
BSR30_API void bsr30_set_radar_frame_callback(bsr30_frame_cb callback);

// API - 레이더 OTA 재부팅
BSR30_API bool bsr30_ota_reboot();

#ifdef __cplusplus
} /* extern "C" */
#endif
