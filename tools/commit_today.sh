#!/bin/bash
# =============================================================================
# 2026-09-03 작업분 마이크로 커밋 스크립트
#
#   ./tools/commit_today.sh          # 커밋 실행
#   ./tools/commit_today.sh --dry    # 무엇이 커밋될지 확인만
#
# 파일이 커밋마다 정확히 한 번씩만 들어가도록 묶어 두었다.
# 커밋 메시지는 기존 이력 형태(ADD./Update./FIXED.)를 따른다.
# =============================================================================
set -e
cd "$( cd "$( dirname "${BASH_SOURCE[0]}" )/.." && pwd )"

DRY=0
[ "$1" = "--dry" ] && DRY=1

commit() {
    local msg="$1"; shift
    echo
    echo "─────────────────────────────────────────────────────────────"
    echo "  $msg"
    for f in "$@"; do echo "    + $f"; done
    if [ $DRY -eq 1 ]; then return 0; fi
    git add -- "$@"
    if git diff --cached --quiet; then
        echo "    (변경 없음 — 건너뜀)"
        return 0
    fi
    git commit -q -m "$msg"
    echo "    → $(git log --oneline -1)"
}

# 1. 3rdparty 리눅스 프리빌트 라이브러리 (x86_64 / aarch64)
commit "ADD. 리눅스 x86_64/aarch64 3rdparty 프리빌트 라이브러리 추가" \
    3rdparty/BSR20_SDK/linux \
    3rdparty/BSR30_SDK/linux \
    3rdparty/FFmpeg/lib/linux \
    3rdparty/OpenSSL/lib/linux \
    3rdparty/glfw/lib/linux \
    3rdparty/imgui/lib/linux \
    3rdparty/libnmea/linux \
    3rdparty/live555/lib/linux \
    3rdparty/ouster-sdk/lib/linux

# 2. 3rdparty 재빌드 / 검증 도구
commit "ADD. 3rdparty 리눅스 빌드 스크립트 및 카메라/플레이어 검증 도구 추가" \
    tools \
    package_deb.sh \
    package_linux.sh \
    .gitignore

# 3. 빌드 시스템 수정
commit "FIXED. 빌드 트리 직접 실행 시 libswresample 미탐색 문제 수정 (RPATH + 명시적 링크)" \
    CMakeLists.txt \
    src/CMakeLists.txt

# 4. 설정/자료구조 — 카메라 2채널 + 시간 기준 옵션
commit "Update. 카메라 2채널 설정 구조 및 시간 기준 옵션(time_source/time_offset_ms) 추가" \
    src/core/app_config.h \
    src/core/app_state.h \
    src/core/session_info.h \
    src/loggers/session_logger.h \
    config.ini

# 5. 프레임 자료구조 — I420 + 압축 패킷
commit "Update. CameraFrame 에 I420 평면 및 H.264 액세스 유닛 구조 추가" \
    src/core/sensor_data.h

# 6. RTSP 워커
commit "ADD. RTSP 카메라 H.264 패스스루 및 촬영 시각(RTCP) 기반 타임스탬프 지원" \
    src/sensors/camera_rtsp_worker.h \
    src/sensors/camera_rtsp_worker.cpp

# 7. 카메라 로거
commit "ADD. CameraLogger 무재인코딩(패스스루) 녹화 모드 및 I420 직결 인코딩 추가" \
    src/loggers/camera_logger.h \
    src/loggers/camera_logger.cpp

# 8. 프리뷰 렌더링
commit "ADD. 프리뷰 YUV->RGB GPU 셰이더 변환 (CPU 색공간 변환 제거)" \
    src/render/camera_texture.h \
    src/render/camera_texture.cpp \
    src/ui/camera_view.cpp

# 9. 플레이어
commit "ADD. 플레이어 카메라 2채널 재생 및 프레임 탐색 성능 개선" \
    src/player/data_loader.h \
    src/player/data_loader.cpp \
    src/player/session_player.h \
    src/player/session_player.cpp \
    src/player/camera_prefetch.h \
    src/player/camera_prefetch.cpp

# 10. UI
commit "ADD. 카메라 2채널 UI(설정/프리뷰 분할) 및 시간 기준 표시 추가" \
    src/ui/main_window.h \
    src/ui/main_window.cpp \
    src/ui/settings_panel.h \
    src/ui/settings_panel.cpp

echo
echo "─────────────────────────────────────────────────────────────"
if [ $DRY -eq 1 ]; then
    echo "  (--dry 모드였습니다. 실제 커밋하려면 인자 없이 실행하세요)"
else
    echo "  완료. 최근 커밋:"
    git log --oneline -10
    echo
    echo "  남은 변경(있다면):"
    git status --short
fi
