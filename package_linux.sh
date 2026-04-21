#!/bin/bash
# Linux x86 배포 패키지 생성

set -e

BUILD_DIR="cmake-build-release-remote-host"
PKG_NAME="MultisensorLogger_linux_x86_v1.0.0"
PKG_DIR="dist/$PKG_NAME"

if [ ! -f "$BUILD_DIR/bin/MultisensorLogger" ]; then
    echo "[ERROR] Build not found. Build Release first."
    exit 1
fi

echo "Creating package in $PKG_DIR..."
rm -rf "$PKG_DIR"
mkdir -p "$PKG_DIR/lib"

# Copy exe
cp "$BUILD_DIR/bin/MultisensorLogger" "$PKG_DIR/"

# Copy config
cp config.ini "$PKG_DIR/"

# Copy all .so files to lib/
cp 3rdparty/libnmea/linux/sdk/lib/libnmea.so*          "$PKG_DIR/lib/" 2>/dev/null || true
cp 3rdparty/BSR20_SDK/linux/sdk/32bit/lib/libBSR20_SDK.so* "$PKG_DIR/lib/" 2>/dev/null || true
cp 3rdparty/BSR30_SDK/linux/sdk/lib/libBSR30_SDK.so*   "$PKG_DIR/lib/" 2>/dev/null || true
cp 3rdparty/BSR30_SDK/linux/sdk/lib/libuv.so*          "$PKG_DIR/lib/" 2>/dev/null || true
cp 3rdparty/ouster-sdk/lib/linux/libshared_library.so  "$PKG_DIR/lib/" 2>/dev/null || true
cp 3rdparty/CameraCapture/lib/linux/libccap.so         "$PKG_DIR/lib/" 2>/dev/null || true
cp 3rdparty/FFmpeg/lib/linux/libav*.so*                "$PKG_DIR/lib/" 2>/dev/null || true
cp 3rdparty/FFmpeg/lib/linux/libswscale.so*            "$PKG_DIR/lib/" 2>/dev/null || true
cp 3rdparty/imgui/lib/linux/libimgui.so*               "$PKG_DIR/lib/" 2>/dev/null || true
cp 3rdparty/glfw/lib/linux/libglfw.so*                 "$PKG_DIR/lib/" 2>/dev/null || true
cp 3rdparty/OpenSSL/lib/linux/lib/libssl.so*           "$PKG_DIR/lib/" 2>/dev/null || true
cp 3rdparty/OpenSSL/lib/linux/lib/libcrypto.so*        "$PKG_DIR/lib/" 2>/dev/null || true

# Create Data folder
mkdir -p "$PKG_DIR/Data"

# Create run.sh
cat > "$PKG_DIR/run.sh" << 'EOF'
#!/bin/bash
SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
export LD_LIBRARY_PATH="$SCRIPT_DIR/lib:$LD_LIBRARY_PATH"

# --- Multicast route setup (only if multicast enabled in config.ini) ---
# On multi-NIC systems, the kernel's default multicast join may pick the wrong
# interface. We detect the NIC that has a route to the sensor IP and bind the
# multicast group to that NIC.
CONFIG_FILE="$SCRIPT_DIR/config.ini"
if [ -f "$CONFIG_FILE" ] && grep -qE "^[[:space:]]*multicast_enabled[[:space:]]*=[[:space:]]*true" "$CONFIG_FILE"; then
    SENSOR_IP=$(awk -F= '/^\[Lidar\]/{in_sec=1;next} /^\[/{in_sec=0} in_sec && /^[[:space:]]*ip[[:space:]]*=/{gsub(/[[:space:]]/,"",$2);print $2;exit}' "$CONFIG_FILE")
    MULTICAST_GROUP=$(awk -F= '/^\[Lidar\]/{in_sec=1;next} /^\[/{in_sec=0} in_sec && /^[[:space:]]*multicast_dest[[:space:]]*=/{gsub(/[[:space:]]/,"",$2);print $2;exit}' "$CONFIG_FILE")

    if [ -n "$SENSOR_IP" ] && [ -n "$MULTICAST_GROUP" ]; then
        SENSOR_NIC=$(ip route get "$SENSOR_IP" 2>/dev/null | sed -n 's/.*dev \([^ ]*\).*/\1/p' | head -1)

        if [ -n "$SENSOR_NIC" ]; then
            CURRENT_NIC=$(ip route show "$MULTICAST_GROUP/32" 2>/dev/null | sed -n 's/.*dev \([^ ]*\).*/\1/p' | head -1)
            if [ "$CURRENT_NIC" != "$SENSOR_NIC" ]; then
                echo "Setting multicast route: $MULTICAST_GROUP -> $SENSOR_NIC (requires sudo)"
                sudo ip route del "$MULTICAST_GROUP/32" 2>/dev/null
                if sudo ip route add "$MULTICAST_GROUP/32" dev "$SENSOR_NIC" 2>/dev/null; then
                    echo "  Route added."
                else
                    echo "  Warning: failed to add route. Run manually:"
                    echo "  sudo ip route add $MULTICAST_GROUP/32 dev $SENSOR_NIC"
                fi
            fi
        fi
    fi
fi

exec "$SCRIPT_DIR/MultisensorLogger" "$@"
EOF
chmod +x "$PKG_DIR/run.sh"
chmod +x "$PKG_DIR/MultisensorLogger"

# README
cat > "$PKG_DIR/README.txt" << 'EOF'
Multi-Sensor Logger (Linux x86)

Requirements:
  - Linux x86_64 with i386 multilib support
  - sudo apt install libzip5:i386 libssl1.1:i386 libcurl4:i386 libpng16-16:i386

Run:
  ./run.sh

Note:
  When Lidar multicast mode is enabled (config.ini: multicast_enabled = true),
  run.sh automatically sets up the kernel multicast route for the correct NIC.
  This requires sudo privileges (you will be prompted for your password once
  per session). If you cannot use sudo, manually add the route:
      sudo ip route add <multicast_group>/32 dev <nic_name>
EOF

# Tarball
cd dist
tar czf "$PKG_NAME.tar.gz" "$PKG_NAME"
cd ..

echo ""
echo "Package created: dist/$PKG_NAME.tar.gz"
