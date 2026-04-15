# Multisensor Logger C++ 재설계 계획

## Context
Python/PySide6 기반의 다중센서 로깅 툴(`proj24_police_13_test_logger`)을 C++ / ImGui 기반으로 재설계.
기존 기능(센서 수집, 실시간 시각화, 녹화, 재생) 전부 유지하면서 크로스플랫폼(Windows/Linux) 지원.

---

## 1. 프로젝트 디렉토리 구조

```
Multisensor_logger/
├── CMakeLists.txt                    # 루트 CMake
├── config.ini                        # 런타임 설정 (config.py 대체)
├── 3rdparty/                         # 기존 써드파티 (그대로 유지)
└── src/
    ├── CMakeLists.txt
    ├── main.cpp                      # 진입점: GLFW + ImGui + 메인루프
    │
    ├── core/                         # 공통 인프라
    │   ├── app_config.h/.cpp         # INI 기반 설정 관리
    │   ├── app_state.h               # 앱 전역 상태 (모드, 녹화상태 등)
    │   ├── clock.h/.cpp              # 타임스탬프 유틸 (pc_ts_rel 계산)
    │   ├── thread_safe_queue.h       # 템플릿 bounded MPSC 큐 (maxsize=500)
    │   ├── event_bus.h/.cpp          # 크로스스레드 이벤트 전달 (Qt Signal 대체)
    │   ├── serial_port.h/.cpp        # 크로스플랫폼 시리얼 (Win32/Linux termios)
    │   └── session_info.h/.cpp       # session_info.ini 읽기/쓰기
    │
    ├── formats/                      # 바이너리 데이터 포맷
    │   ├── rde_format.h/.cpp         # RDE/RDEI 읽기/쓰기 (64비트 비트패킹)
    │   ├── gpsd_format.h/.cpp        # GPSD/GPSDI 52B/16B 레코드
    │   ├── imud_format.h/.cpp        # IMUD/IMUDI 40B/16B 레코드
    │   └── radar_ini.h/.cpp          # 레이더 INI 설정
    │
    ├── sensors/                      # 센서 워커 스레드
    │   ├── sensor_worker.h           # ISensorWorker 추상 기반 클래스
    │   ├── ouster_worker.h/.cpp      # Lidar+IMU (ouster-sdk, pcap 패킷 캡처)
    │   ├── radar20_worker.h/.cpp     # BSR20 (polling: byda_poll)
    │   ├── radar30_worker.h/.cpp     # BSR30 (callback: bsr30_set_radar_frame_callback)
    │   ├── camera_rtsp_worker.h/.cpp # RTSP (live555 이벤트루프 + FFmpeg H.264 디코딩)
    │   ├── camera_usb_worker.h/.cpp  # USB 카메라 (CameraCapture/ccap)
    │   └── gps_worker.h/.cpp         # GPS (SerialPort + libnmea 파싱)
    │
    ├── loggers/                      # 파일 기록기 스레드
    │   ├── base_logger.h             # ILogger 추상 기반 (큐 drain 패턴)
    │   ├── lidar_logger.h/.cpp       # pcap 파일 쓰기 + .pcap.idx 인덱스
    │   ├── radar_logger.h/.cpp       # RDE/RDEI 바이너리 쓰기
    │   ├── camera_logger.h/.cpp      # FFmpeg C API로 MP4 H.264 VFR 인코딩
    │   ├── gps_logger.h/.cpp         # GPSD/GPSDI 바이너리 쓰기
    │   ├── imu_logger.h/.cpp         # IMUD/IMUDI 바이너리 쓰기
    │   └── session_logger.h/.cpp     # 세션 디렉토리 생성 + session_info.ini
    │
    ├── player/                       # 재생 엔진
    │   ├── data_loader.h/.cpp        # 세션 폴더 파싱, 인덱스/타임스탬프 로드
    │   ├── session_player.h/.cpp     # 타임라인 동기화, seek, 가변속도 재생
    │   ├── camera_prefetch.h/.cpp    # 배경 H.264 디코딩 스레드
    │   └── pcap_reader.h/.cpp        # Ouster pcap 재생 (open_source)
    │
    ├── render/                       # OpenGL 렌더링
    │   ├── gl_helpers.h/.cpp         # 셰이더 컴파일, VAO/VBO 유틸
    │   ├── bev_renderer.h/.cpp       # BEV: 라이다 포인트 + 레이더 트랙 (OpenGL)
    │   ├── camera_texture.h/.cpp     # RGB → GL 텍스처 업로드
    │   └── shaders/
    │       ├── point_cloud.vert/.frag
    │       └── radar_marker.vert/.frag
    │
    └── ui/                           # ImGui UI
        ├── main_window.h/.cpp        # 오케스트레이터 (모드전환, 센서연결, 녹화제어)
        ├── settings_panel.h/.cpp     # 센서 설정 + 녹화 제어 패널
        ├── player_panel.h/.cpp       # 재생 제어 패널
        ├── camera_view.h/.cpp        # 카메라 디스플레이 (ImGui::Image)
        ├── sensor_info_view.h/.cpp   # GPS/IMU 텔레메트리 텍스트
        └── bev_view.h/.cpp           # BEV 뷰포트 (FBO → ImGui::Image)
```

---

## 2. 스레딩 모델

```
메인 스레드 (UI):
  GLFW 이벤트 → EventBus drain → ImGui 렌더 → OpenGL BEV → SwapBuffers

센서 워커 스레드 (센서당 1개):
  OusterWorker   → poll_client() 루프 → EventBus에 LidarScanData 푸시
  Radar20Worker  → byda_poll() 루프 → EventBus에 RadarScanData 푸시
  Radar30Worker  → 콜백 → 내부큐 → EventBus에 RadarScanData 푸시
  CameraRtspWorker → live555 doEventLoop() + FFmpeg decode → EventBus에 CameraFrame 푸시
  CameraUsbWorker  → ccap grab() 루프 → EventBus에 CameraFrame 푸시
  GpsWorker      → SerialPort read → nmea_parse() → EventBus에 GpsFix 푸시

로거 스레드 (녹화 중 센서당 1개):
  LidarLogger  → 큐 drain → pcap + .pcap.idx 파일
  RadarLogger  → 큐 drain → RDE + RDEI 파일
  CameraLogger → FFmpeg API로 MP4 인코딩
  GpsLogger    → 큐 drain → GPSD + GPSDI 파일
  IMULogger    → 큐 drain → IMUD + IMUDI 파일

재생 스레드 (재생 중):
  CameraPrefetch → 백그라운드 H.264 디코딩 (슬라이딩 윈도우 버퍼)
```

### 크로스스레드 통신: EventBus
Qt Signal/Slot 대체. 워커 스레드가 `std::function<void()>` 클로저를 EventBus에 push → 메인 스레드가 매 프레임 drain하여 실행.

```cpp
class EventBus {
    std::mutex mtx_;
    std::vector<std::function<void()>> pending_;
public:
    void post(std::function<void()> fn);  // 워커 스레드에서 호출
    void drain();  // 메인 스레드에서 매 프레임 호출
};
```

---

## 3. 핵심 클래스 설계

### ISensorWorker (sensors/sensor_worker.h)
```cpp
class ISensorWorker {
protected:
    std::thread thread_;
    std::atomic<bool> running_{false}, connected_{false};
    std::atomic<bool> recording_{false};
    std::atomic<double> rec_start_time_{0.0};
    std::atomic<uint64_t> frame_count_{0};
    EventBus& event_bus_;
    virtual void pollLoop() = 0;
public:
    void start();  // thread_ 생성
    void stop();   // running_=false, join(5s)
    void setRecording(bool active, double start_time);
    double getRelativeTime() const;
};
```
- Python `BaseSensorWorker(QThread)` 1:1 대응
- 참조: `proj24_police_13_test_logger/sensors/base.py`

### ILogger (loggers/base_logger.h)
```cpp
template<typename T>
class ILogger {
    ThreadSafeQueue<T, 500> queue_;
    std::thread thread_;
    std::atomic<bool> running_{false};
protected:
    virtual void onStart() = 0;
    virtual void writeItem(const T& item) = 0;
    virtual void onStop() = 0;
public:
    void enqueue(T item);  // drop oldest if full
    void start();  // onStart → drain loop → onStop
    void stop();   // running_=false, drain 잔여, join(10s)
};
```
- Python `BaseLogger(QThread)` 1:1 대응
- 참조: `proj24_police_13_test_logger/loggers/base.py`

### ThreadSafeQueue (core/thread_safe_queue.h)
```cpp
template<typename T, size_t MaxSize = 500>
class ThreadSafeQueue {
    std::mutex mtx_;
    std::condition_variable cv_;
    std::deque<T> queue_;
public:
    bool try_push(T item);     // 꽉 차면 front pop 후 push
    bool try_pop(T& out, std::chrono::milliseconds timeout = 500ms);
    size_t size() const;
    void clear();
};
```

---

## 4. 센서별 구현 상세

### Lidar (OusterWorker + LidarLogger)
- **수집**: `ouster::sensor::init_client()` → `poll_client()` → `read_lidar_packet()`/`read_imu_packet()`
- **시각화**: `ScanBatcher` + `XYZLut`으로 XYZ 포인트클라우드 생성 → BEV
- **녹화**: raw UDP 패킷을 pcap 포맷으로 저장 (24B 글로벌 헤더 + 패킷당 16B 헤더 + payload)
- **인덱스**: `.pcap.idx` 사이드카 파일 (타임스탬프 → 파일 오프셋)
- **재생**: `ouster::sensor::open_source(pcap_path)` → LidarScan 순회

### Radar (Radar20Worker / Radar30Worker + RadarLogger)
- **BSR20**: `byda_create()` → `byda_connect()` → `byda_poll()` → `byda_get_track()` (폴링)
- **BSR30**: `bsr30_connect()` → `bsr30_radar_start()` → 프레임 콜백 (콜백→내부큐→EventBus)
- **녹화**: RDE/RDEI 바이너리 (기존 포맷 그대로 유지)
  - RDE: 4B prefix + 8B scan_pattern + 16B header + 40B params + N×8B tracks (64비트 비트패킹)
  - RDEI: 4B count + N×24B records (f64 ts + i64 offset + i32 scan_idx + i32 length)

### Camera RTSP (CameraRtspWorker + CameraLogger)
- **수집**: live555 RTSPClient → DESCRIBE → SETUP → PLAY → H.264 NAL 수신
- **디코딩**: FFmpeg `avcodec_send_packet()` / `avcodec_receive_frame()` + `sws_scale()` (YUV→RGB)
- **인증**: live555 `Authenticator(user, pass)` + OpenSSL (TLS 지원)
- **녹화**: FFmpeg C API로 MP4 VFR 인코딩 (`avformat` muxer, PTS = PC 도착시간)

### Camera USB (CameraUsbWorker)
- **수집**: `ccap::Provider` → `open(device)` → `grab(timeout)` → RGB 변환
- **녹화**: CameraLogger 공유 (동일한 FFmpeg MP4 인코더)

### GPS (GpsWorker + GpsLogger)
- **시리얼**: `SerialPort` (Win32: CreateFile/SetCommState, Linux: termios)
- **파싱**: `nmea_parse()` → `nmea_gpgga_s`, `nmea_gprmc_s`, `nmea_gpgsa_s`, `nmea_gpvtg_s`
- **자동감지**: 포트 열거 + 보레이트 순회 (9600, 115200, 4800)
- **녹화**: GPSD/GPSDI 바이너리 (52B/16B, 기존 포맷 유지)

### IMU (OusterWorker에서 분리 + IMULogger)
- OusterWorker가 `read_imu_packet()`으로 IMU 데이터 수신 → 별도 시그널
- **녹화**: IMUD/IMUDI 바이너리 (40B/16B, 기존 포맷 유지)

---

## 5. 저장 포맷 & 세션 구조

기존 포맷 100% 호환 (기존 Python 재생기로 읽기 가능):

```
session_name/
├── session_info.ini          # [SESSION] [LIDAR] [RADAR] [CAMERA] [GPS]
├── Lidar/
│   ├── session_name.pcap     # ★ 변경: HDF5 → pcap
│   └── session_name.pcap.idx # ★ 신규: 프레임 인덱스 (seek용)
├── Radar/
│   ├── session_name.rde      # 동일
│   ├── session_name.rdei     # 동일
│   └── session_name.ini      # 동일
├── Camera/
│   └── session_name_cam.mp4  # 동일 (H.264 VFR)
├── GPS/
│   ├── session_name.gpsd     # 동일 (52B 레코드)
│   └── session_name.gpsdi    # 동일 (16B 인덱스)
└── IMU/
    ├── session_name.imud     # 동일 (40B 레코드)
    └── session_name.imudi    # 동일 (16B 인덱스)
```

### 타임 싱크
모든 센서 동일: `pc_ts_rel = current_time - recording_start_time`
- 절대시간 = `recording_start_time_unix + pc_ts_rel`

---

## 6. 렌더링 (OpenGL + ImGui)

### BEV 렌더링 (bev_renderer.h)
- **FBO** (Framebuffer Object)에 오프스크린 렌더링 → ImGui::Image()로 표시
- **라이다**: GL_POINTS, 흰색, Z필터 [-20, 10]m, 최대 50k 다운샘플
- **레이더**: GL_POINTS + 커스텀 셰이더 (X마커), 빨간색, 트랙ID 라벨은 ImGui 오버레이
- **감지선**: 노란 점선, Y=30m
- **투영**: 직교투영 (X: -30~30, Y: -10~150)

### 카메라 텍스처 (camera_texture.h)
- RGB 프레임 → `glTexSubImage2D()` → ImGui::Image()

---

## 7. UI 레이아웃 (ImGui)

```
┌──────────────────────────────────────────────────────┐
│ [로깅모드] [플레이어모드]                             │
├──────────┬───────────────────────────────────────────┤
│ 왼쪽패널  │  ┌─────────────┬──────────────┐          │
│ (385px)   │  │ Camera View │ Sensor Info  │          │
│           │  │ (GL 텍스처) │ GPS/IMU 텍스트│          │
│ ▼ Lidar   │  └─────────────┴──────────────┘          │
│  IP/Port  │  ┌─────────────────────────────┐         │
│  [연결]   │  │                             │         │
│           │  │     BEV View (FBO)          │         │
│ ▼ Radar   │  │  Lidar(흰) + Radar(빨강)   │         │
│  IP/Port  │  │                             │         │
│  [연결]   │  └─────────────────────────────┘         │
│           │                                          │
│ ▼ Camera  ├──────────────────────────────────────────┤
│  URL/장치 │  Status: ● REC 00:15:30  |  Frames: ... │
│  [연결]   └──────────────────────────────────────────┘
│           │
│ ▼ GPS     │
│  포트/보레│
│  [연결]   │
│           │
│ [녹화시작]│
│ 경과:00:00│
│ 프레임수  │
└──────────┘
```

플레이어 모드 시 왼쪽패널이 PlayerPanel로 교체:
- 세션 로드, Play/Pause/Seek, 속도조절, 기준센서 선택

---

## 8. 재생 엔진

### DataLoader
- session_info.ini 파싱
- 각 센서 인덱스 파일 로드 (RDEI, GPSDI, IMUDI, .pcap.idx)
- 카메라 MP4 메타데이터 (FFmpeg API로 PTS 배열 추출)

### SessionPlayer
- 기준 센서 타임라인 배열 기준으로 동기 재생
- `seek_to_frame(n)`: `std::lower_bound`로 각 센서 최근접 프레임 검색
- 자동재생: 메인루프에서 타이머 체크, `dt / speed`에 따라 다음 프레임 진행
- 가변속도: 0.5x, 1x, 2x, 4x

### PcapReader (Lidar 재생)
- `ouster::sensor::open_source(pcap_path)` → ScanSource
- 로드 시 전체 스캔 순회하여 타임스탬프 배열 구축 (또는 .pcap.idx 사용)
- `getLidarScan(frame_idx)` → XYZLut 적용 → 포인트클라우드

### CameraPrefetch
- 백그라운드 스레드에서 FFmpeg 디코딩
- 슬라이딩 윈도우 버퍼 (현재 프레임 ±60프레임)
- `av_seek_frame()`으로 seek 지원

---

## 9. CMake 빌드 구조

```cmake
# 루트 CMakeLists.txt
cmake_minimum_required(VERSION 3.20)
project(MultisensorLogger LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 17)

# 플랫폼 감지
if(WIN32)
    set(PLATFORM "window")
else()
    set(PLATFORM "linux")
endif()

# 3rdparty IMPORTED 타겟 정의
# imgui (소스 빌드), glfw, spdlog (header-only)
# ouster-sdk, BSR20_SDK, BSR30_SDK, CameraCapture, libnmea (IMPORTED SHARED)
# live555 (IMPORTED STATIC, 4개 라이브러리)
# FFmpeg: avcodec, avutil, swscale (IMPORTED SHARED)
# OpenSSL (IMPORTED)
find_package(OpenGL REQUIRED)

add_subdirectory(src)

# src/CMakeLists.txt
add_subdirectory(core)      # → libcore.a
add_subdirectory(formats)   # → libformats.a
add_subdirectory(sensors)   # → libsensors.a
add_subdirectory(loggers)   # → libloggers.a
add_subdirectory(player)    # → libplayer.a
add_subdirectory(render)    # → librender.a
add_subdirectory(ui)        # → libui.a

add_executable(MultisensorLogger main.cpp)
target_link_libraries(MultisensorLogger PRIVATE
    core formats sensors loggers player render ui
    imgui glfw OpenGL::GL spdlog
    ouster_sdk bsr20_sdk bsr30_sdk ccap libnmea
    live555 ffmpeg_avcodec ffmpeg_avutil ffmpeg_swscale
    OpenSSL::SSL OpenSSL::Crypto
)

# Windows: POST_BUILD로 DLL 복사
```

---

## 10. 메인 루프 (main.cpp)

```cpp
int main() {
    glfwInit();
    auto* window = glfwCreateWindow(1600, 1000, "Multi-Sensor Logger", NULL, NULL);
    glfwMakeContextCurrent(window);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 330");

    spdlog::set_level(spdlog::level::info);

    MainWindow app;
    app.init();

    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        app.processEvents();   // EventBus drain
        app.render();           // ImGui + OpenGL

        ImGui::Render();
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        glfwSwapBuffers(window);
    }

    app.shutdown();
    glfwTerminate();
    return 0;
}
```

---

## 11. 구현 순서 (권장)

1. **core/** — AppConfig, ThreadSafeQueue, EventBus, SerialPort
2. **render/** + **ui/** — main.cpp + GLFW/ImGui 셋업 + 빈 레이아웃
3. **formats/** — RDE, GPSD, IMUD 바이너리 포맷
4. **sensors/** — 센서별 워커 (GPS부터 → Radar → Camera → Lidar 순)
5. **loggers/** — 센서별 로거
6. **player/** — DataLoader → SessionPlayer → PcapReader → CameraPrefetch
7. **통합 테스트** — 전체 녹화/재생 검증

---

## 12. 주요 참조 파일

| C++ 모듈 | Python 원본 참조 |
|----------|-----------------|
| ISensorWorker | `sensors/base.py` (79줄) |
| ILogger | `loggers/base.py` (64줄) |
| MainWindow | `ui/main_window.py` (850줄) |
| SettingsPanel | `ui/settings_panel.py` (372줄) |
| PlayerPanel | `ui/player_panel.py` (399줄) |
| BevRenderer | `ui/bev_view.py` (139줄) |
| RdeFormat | `formats/rde_writer.py` + `lib/rde_parser.py` |
| GpsdFormat | `formats/gpsd_writer.py` (46줄) |
| ImudFormat | `formats/imud_writer.py` (40줄) |
| SessionPlayer | `players/session_player.py` (400줄) |
| DataLoader | `players/data_loader.py` (300줄) |
| GpsWorker | `sensors/gps_worker.py` + `lib/gps_parser.py` |
