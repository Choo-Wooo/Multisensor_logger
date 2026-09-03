# 윈도우 빌드 & 배포 패키지 추가 가이드 (v2.0.0)

이 문서는 **윈도우에서 Multi-Sensor Logger 를 빌드/검증하고, 그 결과를 리눅스에서 만든
배포 패키지(`dist/MultisensorLogger_v2.0.0/`)에 합치는 절차**를 정리한 것이다.

---

## 0. 중요 — 이 프로젝트의 윈도우 빌드는 **32비트(x86)** 다

`3rdparty/**/window/` 에 들어 있는 DLL/LIB 가 전부 32비트다.

| 라이브러리 | 아키텍처 |
|---|---|
| avcodec-62.dll / avformat-62.dll / avutil-60.dll / swscale-9.dll | x86 (32-bit) |
| glfw3.dll, imgui.dll | x86 |
| shared_library.dll (ouster) | x86 |
| BSR20_SDK.dll (`window/sdk/32bit`), BSR30_SDK.dll | x86 |
| libssl-3.dll / libcrypto-3.dll | x86 |
| ccap.dll | x86 |

따라서 CMake 생성 시 **반드시 `-A Win32`** 를 써야 한다. x64 로 구성하면 링크 단계에서
`module machine type 'x86' conflicts with target machine type 'x64'` 가 난다.

> 참고: 리눅스 빌드에서 문제가 됐던 `libswresample` 은 윈도우 DLL 의존성에 없다.
> (`avformat-62.dll` 의존: avcodec-62, avutil-60, libbz2-1, libgcc_s_dw2-1, msvcrt, zlib1)
> 즉 윈도우는 별도 조치가 필요 없다.

---

## 1. 사전 준비

* **Visual Studio 2022** — "C++를 사용한 데스크톱 개발" 워크로드
  (또는 Build Tools for Visual Studio 2022)
* **CMake 3.16 이상** (VS 에 포함된 것도 가능)
* **Git for Windows**

### 1-1. 저장소 받기

```bat
git clone https://github.com/Choo-Wooo/Multisensor_logger.git
cd Multisensor_logger
```

이미 클론이 있으면 `git pull`. 이때 아래 오류가 나면 **untracked 로 남은 리눅스
바이너리 찌꺼기** 때문이므로 지우고 받는다(윈도우 빌드에는 쓰이지 않는 파일들이다).

```
error: The following untracked working tree files would be overwritten by merge:
    3rdparty/FFmpeg/lib/linux/aarch64/libavcodec.so.62
    ...
```

```bat
git clean -nd 3rdparty      :: 지워질 목록 먼저 확인
git clean -fd 3rdparty      :: 삭제
git pull
```

### 1-2. 심볼릭 링크 주의 (중요)

저장소에는 리눅스용 심볼릭 링크가 50개 들어 있다(`libglfw.so.3 -> libglfw.so.3.4` 등).
윈도우 Git 은 기본값 `core.symlinks=false` 라 이것들이 **"대상 경로가 적힌 작은 텍스트
파일"** 로 체크아웃된다. 빌드에는 영향이 없지만, **그 상태로 커밋하면 리눅스 쪽 심볼릭
링크가 일반 파일로 바뀌어 리눅스 빌드/패키징이 깨진다.**

```bat
git config core.symlinks           :: false 면 정상(윈도우 기본)
git status --short 3rdparty        :: 아무것도 안 뜨는 것이 정상
```

**윈도우에서는 `3rdparty/**/linux/**` 를 절대 커밋하지 말 것.**

---

## 2. 빌드

### 2-1. 명령줄 (권장)

```bat
cd Multisensor_logger

:: 32비트로 구성
cmake -S . -B build -G "Visual Studio 17 2022" -A Win32 -DCMAKE_BUILD_TYPE=Release

:: 빌드
cmake --build build --config Release -j
```

산출물: `build\bin\Release\MultisensorLogger.exe` (+ 필요한 DLL 이 같은 폴더로 복사됨)

* VC++ 런타임을 정적 링크하려면(대상 PC 에 재배포 패키지 설치 불필요):
  ```bat
  cmake -S . -B build -G "Visual Studio 17 2022" -A Win32 -DMSL_STATIC_RUNTIME=ON
  ```

### 2-2. Visual Studio IDE

1. VS 2022 → **폴더 열기** 로 저장소 열기 (CMake 프로젝트로 인식)
2. 구성 드롭다운에서 **x86-Release** 선택 (x64 아님)
3. 빌드 → 전체 빌드

### 2-3. CLion / 기타

툴체인을 **Visual Studio, 아키텍처 x86** 으로 설정. (기본값이 amd64 이므로 반드시 변경)

---

## 3. 실행 & 검증 체크리스트

`build\bin\Release\MultisensorLogger.exe` 를 실행한다. `config.ini` 는 실행 파일 옆이나
`%APPDATA%\MultisensorLogger\config.ini` 에서 읽는다.

### 3-1. 기본 기동
- [ ] 창이 뜨고 로그에 `OpenGL: ...` 이 찍히는가 (OpenGL 3.3 필요)
- [ ] DLL 오류(`avcodec-62.dll` 등)가 없는가 → 없으면 exe 폴더에 DLL 이 다 복사된 것

### 3-2. 카메라 2채널 (v2.0.0 핵심)
`config.ini` 의 `[Camera]`, `[Camera2]` 에 RTSP URL 을 넣고 `enabled = true`.

- [ ] 설정 패널에 **Camera 1 / Camera 2** 섹션이 각각 보이는가
- [ ] 두 채널 Connect → **프리뷰가 좌우 2분할**로 동시에 나오는가
- [ ] 영상이 **뒤집히거나 색이 이상하지 않은가** (GPU YUV→RGB 셰이더 경로 검증)
      · 상하 반전 → 셰이더 UV, 색 이상 → 풀레인지(yuvj420p) 처리 확인 필요
- [ ] 카메라 이름 오른쪽에 빨간 태그가 뜨는가
      · `[NTP MATCH  lat +xx ms]` = 카메라 시각(RTCP/NTP) 기준
      · `[PC CLOCK MATCH (no NTP)]` = 도착 시각 기준 (카메라 NTP 미설정)

### 3-3. 녹화 (H.264 패스스루)
- [ ] Start Recording → 로그에 `CameraLogger: Started (..., MOV passthrough — 재인코딩 없음)`
- [ ] 로그에 `passthrough recording started (3840x2160, extradata NN B)`
- [ ] Stop 후 `CameraLogger: Stopped (passthrough packets=NNN, drops=0, ...)` — **drops=0**
- [ ] `Data\<세션>\Camera\` 에 `_cam.mov`, `_cam2.mov` 두 개가 생겼는가
- [ ] 두 파일이 미디어 플레이어에서 재생되는가 (해상도/코덱이 카메라 원본 그대로)
- [ ] 작업 관리자에서 CPU 사용률 확인 (4K15 2채널이면 한 자릿수~십몇 % 수준이어야 정상)

### 3-4. 플레이어 2채널 재생
- [ ] Player Mode → 세션 경로 입력 → Load Session
- [ ] `Sensors: ... Camera, Camera2` 로 두 채널 인식
- [ ] Play 시 **두 화면이 같이** 재생되고, 재생 속도가 실시간(x1.0)을 따라가는가
- [ ] x2.0 / x4.0 배속도 버벅이지 않는가

### 3-5. 라이다/레이더 동시
- [ ] 라이다·레이더까지 연결한 상태로 3분 녹화 → 로그에 `decode queue full` 경고가 없는가
- [ ] 세션의 `Lidar\*.pcap`, `Radar\*.rde` 크기가 정상적으로 커지는가

---

## 4. 배포 패키지 만들기

### 4-1. 윈도우 패키지 생성

```bat
package_windows.bat build\bin\Release
```

인자를 생략하면 기본값 `cmake-build-release-visual-studio\bin\Release` 를 본다.
결과: `dist\MultisensorLogger_win32_v2.0.0\` + 같은 이름의 `.zip`

패키지 내용: `MultisensorLogger.exe`, 모든 `*.dll`, `config.ini`, 빈 `Data\`, `README.txt`

### 4-2. 통합 배포 폴더에 합치기

리눅스에서 만들어 옮겨 둔 배포 폴더는 다음 구조다.

```
dist\MultisensorLogger_v2.0.0\
├── README.txt
├── linux-x86_64\      (Ubuntu 20.04 x86_64, 104MB)
├── linux-aarch64\     (Jetson aarch64, 91MB)
└── windows-x86\       ← 여기에 넣는다 (지금은 PLACEHOLDER.txt 만 있음)
```

**넣는 방법** — `dist\MultisensorLogger_win32_v2.0.0\` 의 *내용물* 을 `windows-x86\` 안으로 복사:

```bat
:: 자리표시자 제거
del dist\MultisensorLogger_v2.0.0\windows-x86\PLACEHOLDER.txt

:: 내용물 복사 (폴더째가 아니라 안의 파일들)
xcopy /e /i /y dist\MultisensorLogger_win32_v2.0.0\* dist\MultisensorLogger_v2.0.0\windows-x86\
```

PowerShell 이면:
```powershell
Remove-Item dist\MultisensorLogger_v2.0.0\windows-x86\PLACEHOLDER.txt -ErrorAction SilentlyContinue
Copy-Item dist\MultisensorLogger_win32_v2.0.0\* dist\MultisensorLogger_v2.0.0\windows-x86\ -Recurse -Force
```

완성 후 구조 확인:
```
dist\MultisensorLogger_v2.0.0\windows-x86\
├── MultisensorLogger.exe
├── avcodec-62.dll, avformat-62.dll, avutil-60.dll, swscale-9.dll
├── glfw3.dll, imgui.dll, shared_library.dll, ccap.dll
├── BSR20_SDK.dll, BSR30_SDK.dll, nmea.dll
├── libssl-3.dll, libcrypto-3.dll, libx264-165.dll, zlib1.dll, ...
├── config.ini
├── README.txt
└── Data\
```

### 4-3. 전체 묶기 (선택)

```powershell
Compress-Archive -Path dist\MultisensorLogger_v2.0.0 -DestinationPath dist\MultisensorLogger_v2.0.0_all.zip -Force
```

---

## 5. 커밋 시 주의

윈도우에서 커밋할 때는 **소스 변경분만** 올린다.

```bat
git status --short
:: 3rdparty/**/linux/** 가 보이면 커밋에서 제외할 것 (심볼릭 링크 → 일반 파일 변질 방지)
git add src CMakeLists.txt package_windows.bat
git commit -m "FIXED. 윈도우 빌드 ..."
```

`dist\`, `build*\`, `imgui.ini` 는 `.gitignore` 에 들어 있어 자동으로 제외된다.

---

## 6. 문제 해결

| 증상 | 원인 / 조치 |
|---|---|
| `module machine type 'x86' conflicts with 'x64'` | x64 로 구성함 → `-A Win32` 로 다시 구성 (`build` 폴더 삭제 후) |
| `MultisensorLogger.exe - 시스템 오류: avcodec-62.dll 없음` | exe 와 같은 폴더에 DLL 이 없음 → CMake 의 copy 단계 확인, 또는 `3rdparty\FFmpeg\lib\window\*.dll` 수동 복사 |
| 실행 직후 종료, 로그에 `Failed to initialize GLAD` | OpenGL 3.3 미지원(원격 데스크톱/가상머신) → 실제 GPU 세션에서 실행 |
| 프리뷰가 상하 반전 | GPU 셰이더 UV 문제 — `src/render/camera_texture.cpp` 의 `kYuvVert` 확인 |
| 프리뷰 색이 뿌옇거나 대비 이상 | 풀레인지(yuvj420p) 플래그 — `CameraFrame::yuv_full_range` 전달 확인 |
| 녹화 파일이 안 생김 / 0바이트 | 패스스루는 첫 키프레임(IDR)부터 기록 → 카메라 GOP 가 길면 수 초 지연. 로그의 `passthrough recording started` 확인 |
| 카메라 태그가 계속 `[TIME: checking...]` | RTCP Sender Report 미수신 → 몇 초 대기, 그래도 안 되면 `[PC CLOCK MATCH]` 로 동작(정상) |
| `git pull` 시 untracked 덮어쓰기 오류 | 1-1 항목의 `git clean -fd 3rdparty` 참고 |
