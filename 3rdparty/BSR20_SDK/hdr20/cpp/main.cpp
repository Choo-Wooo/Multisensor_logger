#include "Bsr20Sdk.h"

#include <atomic>
#include <csignal>
#include <cstdio>
#include <iostream>
#include <string>

static std::atomic<bool> g_running{true};

static void onSignal(int) {
    g_running = false;
}

// ------------------------------------------------------------
// Frame callback - called by SDK when a complete 4D radar
// frame has been assembled from the TCP stream.
// NOTE: runs on the receive thread. BydaFrameHandle is only
//       valid inside this callback.
// ------------------------------------------------------------
static void onFrame(BydaFrameHandle frame, void* /*ctx*/) {
    uint32_t frameNum       = byda_frame_get_num(frame);
    int dynHighCount        = byda_frame_get_dyn_high_count(frame);
    int dynLowCount         = byda_frame_get_dyn_low_count(frame);
    int mmLongHighCount     = byda_frame_get_mm_long_high_count(frame);
    int mmLongLowCount      = byda_frame_get_mm_long_low_count(frame);
    int mmShortHighCount    = byda_frame_get_mm_short_high_count(frame);
    int mmShortLowCount     = byda_frame_get_mm_short_low_count(frame);
    int trackCount          = byda_frame_get_track_count(frame);

    std::cout << "[Radar] frame: " << frameNum
              << " | dynHigh: " << dynHighCount
              << " | dynLow: " << dynLowCount
              << " | mmLongHigh: " << mmLongHighCount
              << " | mmLongLow: " << mmLongLowCount
              << " | mmShortHigh: " << mmShortHighCount
              << " | mmShortLow: " << mmShortLowCount
              << " | tracks: " << trackCount
              << std::endl;

    // Dynamic-high point cloud
    for (int i = 0; i < dynHighCount; i++) {
        float x, y, z, vel;
        uint16_t snr;
        if (byda_frame_get_dyn_high_point(frame, i, &x, &y, &z, &vel, &snr) == BYDA_OK) {
            std::cout << "  [dyn_high] idx=" << i
                      << " x=" << x
                      << " y=" << y
                      << " z=" << z
                      << " vel=" << vel
                      << " snr=" << snr
                      << std::endl;
        }
    }

    // Track points
    for (int i = 0; i < trackCount; i++) {
        float x, y, z;
        if (byda_frame_get_track_point(frame, i, &x, &y, &z) == BYDA_OK) {
            std::cout << "  [track] idx=" << i
                      << " x=" << x
                      << " y=" << y
                      << " z=" << z
                      << std::endl;
        }
    }
}

// ------------------------------------------------------------
// main
// ------------------------------------------------------------
int main(int argc, char* argv[]) {
    std::string ip = "192.168.172.128";
    int port = 7;

    if (argc >= 2) ip = argv[1];
    if (argc >= 3) port = std::stoi(argv[2]);

    std::cout << "========================================" << std::endl;
    std::cout << "  HDR20 (4D Radar) SDK Sample" << std::endl;
    std::cout << "  SDK Version: " << byda_get_sdk_version() << std::endl;
    std::cout << "========================================" << std::endl;

    std::signal(SIGINT, onSignal);
    std::signal(SIGTERM, onSignal);

    // Create SDK handle
    BydaRadarHandle handle = byda_create();
    if (!handle) {
        std::cerr << "Failed to create radar handle." << std::endl;
        return 1;
    }

    byda_set_address(handle, ip.c_str(), port);

    // Register frame callback
    byda_set_frame_callback(handle, onFrame, nullptr);

    // Connect to radar
    std::cout << "Connecting to " << ip << ":" << port << "..." << std::endl;
    if (byda_connect(handle) != BYDA_OK) {
        std::cerr << "Connection failed." << std::endl;
        byda_destroy(handle);
        return 1;
    }

    std::cout << "Connected to Radar!" << std::endl;
    std::cout << "Press Ctrl+C to quit." << std::endl;

    // Receive loop
    int loopCount = 0;
    while (g_running) {
        int ret = byda_receive(handle);
        loopCount++;

        if (ret > 0) {
            // 수신 데이터를 hex로 출력 (처음 64바이트만)
            printf("[RECV #%d] %d bytes: ", loopCount, ret);
            // raw 콜백 없이 직접 확인하려면 아래 주석 해제
            // for (int i = 0; i < (ret < 64 ? ret : 64); i++)
            //     printf("%02X ", ((unsigned char*)&ret)[i]);
            printf("\n");
            fflush(stdout);
        }

        if (ret == 0) {
            printf("[EXIT] Connection closed by radar (ret=0, loop=%d)\n", loopCount);
            break;
        }
        if (ret < 0) {
            printf("[EXIT] byda_receive error (ret=%d, loop=%d)\n", ret, loopCount);
            break;
        }
    }
    if (!g_running) {
        printf("[EXIT] Ctrl+C detected (loop=%d)\n", loopCount);
    }

    // Cleanup
    byda_disconnect(handle);
    byda_destroy(handle);

    std::cout << "Closing program..." << std::endl;
    return 0;
}
