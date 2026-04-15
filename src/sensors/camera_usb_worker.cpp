#include "camera_usb_worker.h"
#include <spdlog/spdlog.h>
#include <cstring>

#ifdef _WIN32

// CameraCapture pure C API (Windows-only: no Linux prebuilt binary)
#include <ccap_c.h>

namespace msl {

bool CameraUsbWorker::onConnect() {
    CcapProvider* provider = ccap_provider_create();
    if (!provider) {
        notifyError("USB Camera: Failed to create provider");
        return false;
    }

    if (!ccap_provider_open_by_index(provider, device_index_, true)) {
        notifyError("USB Camera: Failed to open device " + std::to_string(device_index_));
        ccap_provider_destroy(provider);
        return false;
    }

    // Set resolution and pixel format
    ccap_provider_set_property(provider, CCAP_PROPERTY_WIDTH, width_);
    ccap_provider_set_property(provider, CCAP_PROPERTY_HEIGHT, height_);
    ccap_provider_set_property(provider, CCAP_PROPERTY_PIXEL_FORMAT_OUTPUT, CCAP_PIXEL_FORMAT_RGB24);

    provider_ = provider;
    spdlog::info("USB Camera: Opened device {} ({}x{})", device_index_, width_, height_);
    return true;
}

void CameraUsbWorker::pollLoop() {
    auto* provider = static_cast<CcapProvider*>(provider_);
    if (!provider) return;

    while (running_ && !disconnect_requested_) {
        CcapVideoFrame* vf = ccap_provider_grab(provider, 1000);
        if (vf) {
            CcapVideoFrameInfo info;
            if (ccap_video_frame_get_info(vf, &info)) {
                CameraFrame frame;
                frame.width  = info.width;
                frame.height = info.height;

                if (recording_) {
                    frame.pc_ts_rel = getRelativeTime();
                }

                // Copy RGB data
                int size = info.width * info.height * 3;
                frame.rgb_data.resize(size);
                if (info.data[0]) {
                    std::memcpy(frame.rgb_data.data(), info.data[0], size);
                }

                frame_count_++;

                if (on_frame_ready) {
                    on_frame_ready(frame);
                }
            }
            ccap_video_frame_release(vf);
        }
    }
}

void CameraUsbWorker::onDisconnect() {
    if (provider_) {
        auto* provider = static_cast<CcapProvider*>(provider_);
        ccap_provider_close(provider);
        ccap_provider_destroy(provider);
        provider_ = nullptr;
    }
    spdlog::info("USB Camera: Disconnected");
}

} // namespace msl

#else  // non-Windows: stub implementation (ccap Linux binary not available)

namespace msl {

bool CameraUsbWorker::onConnect() {
    notifyError("USB Camera: Not supported on this platform. Use RTSP instead.");
    return false;
}

void CameraUsbWorker::pollLoop() {
    // No-op: stub
}

void CameraUsbWorker::onDisconnect() {
    // No-op: stub
}

} // namespace msl

#endif
