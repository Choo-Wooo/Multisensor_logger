#!/bin/bash
# Debian 패키지(.deb) 생성 — package_linux.sh 로 만든 배포 디렉토리를 그대로 담는다.
#   ./package_deb.sh [arch] [build_dir]
#
# 설치 위치:
#   /opt/MultisensorLogger/            (실행파일 + lib/ + config.ini)
#   /usr/bin/multisensorlogger         (런처 심볼릭 스크립트)
#   /usr/share/applications/...desktop (앱 메뉴 등록)
#   ~/MultisensorData/                 (최초 실행 시 생성되는 기본 데이터 폴더)
set -e

ARCH="${1:-$(uname -m)}"
case "$ARCH" in
    aarch64|arm64) ARCH=aarch64; DEB_ARCH=arm64 ;;
    x86_64|amd64)  ARCH=x86_64;  DEB_ARCH=amd64 ;;
    i[3-6]86|x86)  ARCH=x86;     DEB_ARCH=i386  ;;
    *) echo "[ERROR] Unsupported arch: $ARCH"; exit 1 ;;
esac
BUILD_DIR="${2:-build_aarch64}"
VERSION="1.0.0"
PKG_NAME="MultisensorLogger_linux_${ARCH}_v${VERSION}"
SRC_DIR="dist/$PKG_NAME"

# 배포 디렉토리가 없으면 먼저 만든다
if [ ! -d "$SRC_DIR" ]; then
    echo "[INFO] $SRC_DIR 없음 — package_linux.sh 먼저 실행"
    bash package_linux.sh "$ARCH" "$BUILD_DIR"
fi

DEB_ROOT="dist/deb_${ARCH}"
APP_DIR="$DEB_ROOT/opt/MultisensorLogger"
rm -rf "$DEB_ROOT"
mkdir -p "$APP_DIR" "$DEB_ROOT/DEBIAN" "$DEB_ROOT/usr/bin" \
         "$DEB_ROOT/usr/share/applications" "$DEB_ROOT/usr/share/pixmaps"

cp -a "$SRC_DIR/MultisensorLogger" "$SRC_DIR/lib" "$SRC_DIR/config.ini" "$APP_DIR/"

# 런처: 번들 lib 경로를 잡고 실행 (설치본 config.ini 는 읽기 전용이라
# 사용자별 설정은 ~/.config/MultisensorLogger/config.ini 를 쓴다)
cat > "$DEB_ROOT/usr/bin/multisensorlogger" << 'EOF'
#!/bin/bash
APP_DIR=/opt/MultisensorLogger
export LD_LIBRARY_PATH="$APP_DIR/lib:$LD_LIBRARY_PATH"

mkdir -p "$HOME/.config/MultisensorLogger" "$HOME/.local/share/MultisensorLogger"
if [ ! -f "$HOME/.config/MultisensorLogger/config.ini" ]; then
    cp "$APP_DIR/config.ini" "$HOME/.config/MultisensorLogger/config.ini"
fi

LOG="$HOME/.local/share/MultisensorLogger/last_run.log"

# --- 실행 전 자가 진단: 없는 공유 라이브러리 / GL 확인 -----------------------
MISSING=$(ldd "$APP_DIR/MultisensorLogger" 2>/dev/null | awk '/not found/ {print "  - "$1}')
if [ -n "$MISSING" ]; then
    {
        echo "[ERROR] 필요한 시스템 라이브러리가 없습니다:"
        echo "$MISSING"
        echo "다음을 설치하세요: sudo apt install -f   (또는 위 라이브러리를 제공하는 패키지)"
    } | tee "$LOG" >&2
    command -v zenity >/dev/null 2>&1 &&         zenity --error --width=480 --text="$(cat "$LOG")" 2>/dev/null
    exit 1
fi
if [ -z "$DISPLAY" ] && [ -z "$WAYLAND_DISPLAY" ]; then
    echo "[ERROR] DISPLAY 가 없습니다. GUI 세션(또는 ssh -X)에서 실행하세요." | tee "$LOG" >&2
    exit 1
fi

cd "$HOME"
# 로그를 파일에도 남긴다 (크래시 원인 파악용)
exec > >(tee "$LOG") 2>&1
exec "$APP_DIR/MultisensorLogger" "$@"
EOF
chmod 755 "$DEB_ROOT/usr/bin/multisensorlogger"

cat > "$DEB_ROOT/usr/share/applications/multisensorlogger.desktop" << 'EOF'
[Desktop Entry]
Type=Application
Name=Multi-Sensor Logger
Comment=LiDAR / Radar / Camera / GPS / IMU 동기 녹화 및 재생
Exec=multisensorlogger
Icon=multisensorlogger
Terminal=false
Categories=Utility;Science;
EOF

[ -f assets/icons/icon_66x67.png ] && \
    cp assets/icons/icon_66x67.png "$DEB_ROOT/usr/share/pixmaps/multisensorlogger.png"

INSTALLED_KB=$(du -sk "$DEB_ROOT" | cut -f1)
cat > "$DEB_ROOT/DEBIAN/control" << EOF
Package: multisensorlogger
Version: $VERSION
Section: utils
Priority: optional
Architecture: $DEB_ARCH
Maintainer: BYDA <support@byda.co.kr>
Depends: libc6, libstdc++6, libgl1, libx11-6, libxrandr2, libxinerama1, libxcursor1, libxi6, libcurl4, libzip5
Installed-Size: $INSTALLED_KB
Description: Multi-Sensor Logger
 LiDAR(Ouster), Radar(BYDA BSR20/BSR30), RTSP/USB 카메라, GPS, IMU 를
 동기 녹화하고 타임라인으로 재생하는 도구.
 FFmpeg / live555 / ouster-sdk / BYDA SDK 는 /opt/MultisensorLogger/lib 에 동봉.
EOF

cat > "$DEB_ROOT/DEBIAN/postinst" << 'EOF'
#!/bin/bash
set -e
# 데스크톱 DB 갱신 (있을 때만)
command -v update-desktop-database >/dev/null 2>&1 && update-desktop-database -q || true
echo "Multi-Sensor Logger 설치 완료. 실행: multisensorlogger"
exit 0
EOF
chmod 755 "$DEB_ROOT/DEBIAN/postinst"

DEB_FILE="dist/multisensorlogger_${VERSION}_${DEB_ARCH}.deb"
dpkg-deb --build --root-owner-group "$DEB_ROOT" "$DEB_FILE" > /dev/null
rm -rf "$DEB_ROOT"

echo ""
echo "Package created: $DEB_FILE"
echo "  설치:   sudo apt install ./$DEB_FILE      (또는 sudo dpkg -i)"
echo "  실행:   multisensorlogger"
echo "  제거:   sudo apt remove multisensorlogger"
