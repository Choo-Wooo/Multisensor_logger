# Multi-Sensor Logger

A cross-platform multi-sensor data acquisition and playback tool for autonomous driving and vehicle testing scenarios. Records synchronized data from LiDAR, radar, camera, GPS, and IMU sensors, and provides a timeline-based player for recorded sessions.

![Main](docs/images/main.gif)

---

## Features

- **Synchronized recording** from up to five sensor types simultaneously
- **Real-time visualization**:
  - Bird's Eye View (BEV) with LiDAR point cloud and radar tracks
  - Live camera feed (RTSP H.264 or USB)
  - GPS trajectory map with zoom/pan
  - IMU telemetry
- **Playback engine** with timeline synchronization, seek, and variable speed (0.5x to 4x)
- **Native data formats** preserved for interoperability:
  - LiDAR: `.pcap` (Ouster Studio compatible)
  - Radar: `.rde` / `.rdei` (bit-packed binary)
  - Camera: `.mp4` (H.264 VFR)
  - GPS: `.gpsd` / `.gpsdi` (fixed-size records)
  - IMU: `.imud` / `.imudi`
- **Cross-platform**: Windows x86 and Linux x86
- **Built with** Dear ImGui + GLFW + OpenGL (no Qt dependency)

---

## Screenshots

### Logging Mode
*Real-time sensor visualization during recording.*

![Logging](docs/images/logging.png)

### Player Mode
*Synchronized playback of a recorded session.*

![Player](docs/images/player.png)

### Bird's Eye View
*LiDAR point cloud with radar tracks colored by velocity (red = stationary, green = moving).*

![BEV](docs/images/bev.png)

### GPS Map
*Embedded GPS trajectory viewer with coordinate grid and scale bar.*

![GPS Map](docs/images/gps_map.png)

---

## Supported Sensors

| Sensor   | Model                 | Interface                 | Storage Format                    |
| -------- | --------------------- | ------------------------- | --------------------------------- |
| LiDAR    | Ouster OS series      | UDP (lidar 7502/imu 7503) | `.pcap` + sidecar index           |
| Radar    | BYDA BSR20            | TCP (default port 7)      | `.rde` / `.rdei`                  |
| Radar    | BYDA BSR30            | TCP + UDP                 | `.rde` / `.rdei`                  |
| Camera   | RTSP (H.264)          | live555 + FFmpeg          | `.mp4` H.264 VFR                  |
| Camera   | USB webcam            | DirectShow / V4L2 (ccap)  | `.mp4` H.264 VFR                  |
| GPS      | NMEA 0183             | USB serial                | `.gpsd` / `.gpsdi` (52-byte rec)  |
| IMU      | Ouster built-in       | UDP (with LiDAR)          | `.imud` / `.imudi` (40-byte rec)  |

---

## Project Structure

```
Multisensor_logger/
├── 3rdparty/             Pre-built third-party libraries (Windows + Linux)
├── docs/                 Screenshots and documentation
├── src/
│   ├── core/             Config, EventBus, ThreadSafeQueue, SerialPort, LatestValue
│   ├── formats/          Binary formats (RDE, GPSD, IMUD)
│   ├── sensors/          Sensor workers (one thread per sensor)
│   ├── loggers/          File writers (queue-based async)
│   ├── player/           Playback engine (pcap reader, MP4 decoder)
│   ├── render/           OpenGL rendering (BEV FBO, camera texture)
│   └── ui/               ImGui UI (panels, views)
├── config.ini            Runtime configuration
├── CMakeLists.txt        Root CMake
└── README.md
```

---

## Quick Start

### Prerequisites

**Windows (x86)**
- Visual C++ Redistributable (x86): https://aka.ms/vs/17/release/vc_redist.x86.exe

**Linux (x86)**
- Enable i386 architecture and install 32-bit runtime libraries:

```bash
sudo dpkg --add-architecture i386
sudo apt update
sudo apt install libc6:i386 libstdc++6:i386 libzip5:i386 \
                 libssl1.1:i386 libcurl4:i386 libpng16-16:i386 \
                 libuv1:i386 libx264-155:i386
```

### Run Pre-built Release

1. Download the latest release from the Releases page
2. Extract the archive
3. Edit `config.ini` with your sensor IP addresses and ports
4. Launch:
   - Windows: `MultisensorLogger.exe`
   - Linux: `./run.sh` (sets `LD_LIBRARY_PATH` to bundled libs)

---

## Build from Source

### Windows (Visual Studio 2022)

Requirements:
- Visual Studio 2022 with MSVC 14.44+
- CMake 3.16+

```bash
cmake -B build -G "Visual Studio 17 2022" -A Win32
cmake --build build --config Release
```

Output: `build/bin/Release/MultisensorLogger.exe`

All runtime DLLs are copied next to the executable automatically.

### Linux (x86 32-bit)

Build-time requirements:

```bash
sudo apt install gcc-multilib g++-multilib cmake
sudo apt install libzip-dev:i386 libssl-dev:i386 \
                 libcurl4-openssl-dev:i386 libpng-dev:i386 \
                 libuv1-dev:i386 libx264-dev:i386
```

Configure and build:

```bash
cmake -B build \
      -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_C_FLAGS=-m32 \
      -DCMAKE_CXX_FLAGS=-m32
cmake --build build -j$(nproc)
```

Output: `build/bin/MultisensorLogger`

---

## Usage

### Logging Mode

1. Click **Connect** on each sensor you want to record.
2. For RTSP cameras, click **Test** first to auto-detect the native resolution. A resolution preset list is then generated based on the native aspect ratio; select one if you want recordings resized.
3. For USB webcams, click **Find** to enumerate connected devices.
4. Set the **Data Directory** (defaults to `./Data` under the executable).
5. Optionally enter a **Session Name**. Leave empty to use an auto-generated timestamp (`rec_YYYYMMDD_HHMMSS`).
6. Click **Start Recording**. Frame counts and FPS per sensor are displayed.
7. Click **Stop Recording** when done. File finalization (FFmpeg encoder flush, index writing) runs in a background thread so the UI stays responsive.

### Player Mode

1. Click **Player Mode** at the top.
2. Click **Browse...** and select a session folder (e.g., `Data/rec_20260415_120000`).
3. Click **Load Session**.
4. Use the transport controls (Play/Pause/Prev/Next/Seek) and speed selector (0.5x / 1x / 2x / 4x).
5. Choose a **Reference Sensor** from the dropdown. All other sensors are synchronized to its timeline via nearest-neighbor timestamp matching.

### BEV View Controls

- **Scroll wheel** - zoom in/out (centered on cursor)
- **Middle-mouse drag** - pan
- **Right click** - reset view to default range

### GPS Map Controls

- **Scroll wheel** - zoom
- **Middle-mouse drag** - pan (disables auto-follow)
- **Right click** - re-enable auto-follow on current position

---

## Recording Format

Each recording creates a timestamped session directory:

```
rec_20260415_120000/
├── session_info.ini             Session metadata (UTC start time, sensor configs)
├── Lidar/
│   ├── rec_*.pcap               Raw UDP packets (Ethernet/IP/UDP framed)
│   ├── rec_*.pcap.idx           Per-scan timestamp and byte offset index
│   └── rec_*_meta.json          Ouster sensor metadata (required for playback)
├── Radar/
│   ├── rec_*.rde                Binary track data with bit-packed fields
│   ├── rec_*.rdei               Per-scan timestamp index
│   └── rec_*.ini                Radar installation parameters
├── Camera/
│   └── rec_*_cam.mp4            H.264 variable-frame-rate
├── GPS/
│   ├── rec_*.gpsd               52-byte fixed records
│   └── rec_*.gpsdi              16-byte index (timestamp + offset)
└── IMU/
    ├── rec_*.imud               40-byte fixed records
    └── rec_*.imudi              16-byte index
```

All timestamps use `pc_ts_rel` - seconds since the recording start time. Absolute time is `recording_start_time_unix + pc_ts_rel`.

The LiDAR `.pcap` file is compatible with Ouster Studio when opened alongside `_meta.json`.

---

## Configuration

`config.ini` controls default sensor settings:

```ini
[Lidar]
ip = 192.168.172.129
udp_dest = 239.201.201.201
mtp_main = false
mtp_dest = 192.168.172.18
lidar_port = 7502
imu_port = 7503

[Radar]
ip = 192.168.172.128
port = 7
udp_port = 9002
sdk = BSR20

[Camera]
type = RTSP
rtsp_url = rtsp://user:password@192.168.1.100/stream
width = 1920
height = 1080
fps = 30

[GPS]
port =
baudrate = 9600

[Recording]
data_dir = Data

[Map]
origin_lat = 37.555503
origin_lon = 126.973751
```

For a shared Ouster multicast stream, set `udp_dest` to the multicast group, keep `mtp_main = false` on this logger, and set `mtp_dest` to the local NIC IP that should join the group.

Empty `GPS port` enables auto-detection. Changes made in the UI are saved back to `config.ini` on exit.

---

## Third-Party Libraries

| Library                                                      | Version | Purpose             | License        |
| ------------------------------------------------------------ | ------- | ------------------- | -------------- |
| [Dear ImGui](https://github.com/ocornut/imgui)               | 1.92    | GUI framework       | MIT            |
| [GLFW](https://www.glfw.org/)                                | 3.4     | Window / input      | Zlib           |
| [GLAD](https://glad.dav1d.de/)                               | 3.3     | OpenGL loader       | MIT            |
| [spdlog](https://github.com/gabime/spdlog)                   | 1.x     | Logging             | MIT            |
| [Ouster SDK](https://github.com/ouster-lidar/ouster_sdk)     | 0.16.1  | LiDAR driver        | BSD-3-Clause   |
| [live555](http://www.live555.com/liveMedia/)                 | 2024    | RTSP client         | LGPL-2.1+      |
| [FFmpeg](https://ffmpeg.org/)                                | n7.x    | H.264 encode/decode | LGPL-2.1+ / GPL |
| [libx264](https://www.videolan.org/developers/x264.html)     | -       | H.264 encoder       | GPL-2.0+       |
| [OpenSSL](https://www.openssl.org/)                          | 3.5     | TLS (RTSP auth)     | Apache-2.0     |
| [libnmea](https://github.com/jacketizer/libnmea)             | 0.1.2   | NMEA 0183 parsing   | MIT            |
| [CameraCapture (ccap)](https://github.com/wysaid/CameraCapture) | -       | USB camera          | MIT            |
| BYDA BSR20 SDK                                               | 2.0.0   | BSR20 radar         | Proprietary    |
| BYDA BSR30 SDK                                               | -       | BSR30 radar         | Proprietary    |

Because this project links `libx264` (GPL-2.0+), the resulting binary distribution is effectively subject to GPL terms.

---

## License

This project includes GPL-licensed components (libx264 via FFmpeg) and proprietary components (BYDA BSR20/BSR30 SDKs). Distribution terms depend on those components. See the individual LICENSE files in `3rdparty/` for details.
