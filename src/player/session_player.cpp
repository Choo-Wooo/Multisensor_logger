#include "session_player.h"
#include "core/clock.h"
#include "formats/rde_format.h"
#include <spdlog/spdlog.h>
#include <cmath>
#include <cstring>

namespace msl {

bool SessionPlayer::loadSession(const std::string& session_dir) {
    playing_ = false;
    current_frame_ = 0;

    // Close previous resources
    if (camera_prefetch_) { camera_prefetch_->close(); camera_prefetch_.reset(); }
    if (pcap_reader_) { pcap_reader_->close(); pcap_reader_.reset(); }

    if (!DataLoader::loadSession(session_dir, session_data_)) {
        return false;
    }

    // Open lidar pcap reader if pcap + metadata exist
    if (session_data_.has_lidar && !session_data_.pcap_path.empty() &&
        !session_data_.lidar_meta_path.empty()) {
        pcap_reader_ = std::make_unique<PcapReader>();
        if (pcap_reader_->open(session_data_.pcap_path, session_data_.lidar_meta_path)) {
            session_data_.lidar_timestamps = pcap_reader_->timestamps();
            spdlog::info("SessionPlayer: Lidar loaded ({} scans)", pcap_reader_->frameCount());
        } else {
            pcap_reader_.reset();
            spdlog::warn("SessionPlayer: Lidar pcap loading failed");
        }
    }

    // Open camera prefetch if MP4 exists
    if (session_data_.has_camera && !session_data_.mp4_path.empty()) {
        camera_prefetch_ = std::make_unique<CameraPrefetch>();
        if (camera_prefetch_->open(session_data_.mp4_path)) {
            // Use camera PTS as timestamps
            session_data_.camera_timestamps = camera_prefetch_->timestamps();
            session_data_.camera_frame_count = camera_prefetch_->frameCount();
            camera_prefetch_->start();
        } else {
            camera_prefetch_.reset();
            session_data_.has_camera = false;
        }
    }

    buildReferenceTimeline();
    return true;
}

void SessionPlayer::setReferenceSensor(const std::string& sensor) {
    ref_sensor_ = sensor;
    buildReferenceTimeline();
    current_frame_ = 0;
}

void SessionPlayer::buildReferenceTimeline() {
    ref_timestamps_.clear();

    // Try requested sensor
    auto trySet = [&](const std::string& name, bool has, const std::vector<double>& ts) {
        if (ref_sensor_ == name && has && !ts.empty()) {
            ref_timestamps_ = ts;
            return true;
        }
        return false;
    };

    if (trySet("Lidar",  session_data_.has_lidar,  session_data_.lidar_timestamps)) {}
    else if (trySet("Radar",  session_data_.has_radar,  session_data_.radar_timestamps)) {}
    else if (trySet("Camera", session_data_.has_camera, session_data_.camera_timestamps)) {}
    else if (trySet("GPS",    session_data_.has_gps,    session_data_.gps_timestamps)) {}
    else if (trySet("IMU",    session_data_.has_imu,    session_data_.imu_timestamps)) {}

    // Fallback: pick first sensor with actual timestamps (no recursion)
    if (ref_timestamps_.empty()) {
        struct { const char* name; bool has; const std::vector<double>* ts; } sensors[] = {
            {"Lidar",  session_data_.has_lidar,  &session_data_.lidar_timestamps},
            {"Radar",  session_data_.has_radar,  &session_data_.radar_timestamps},
            {"Camera", session_data_.has_camera, &session_data_.camera_timestamps},
            {"GPS",    session_data_.has_gps,    &session_data_.gps_timestamps},
            {"IMU",    session_data_.has_imu,    &session_data_.imu_timestamps},
        };
        for (auto& s : sensors) {
            if (s.has && s.ts && !s.ts->empty()) {
                ref_sensor_ = s.name;
                ref_timestamps_ = *s.ts;
                break;
            }
        }
    }

    spdlog::info("SessionPlayer: Reference '{}', {} frames", ref_sensor_, ref_timestamps_.size());
}

void SessionPlayer::play() {
    if (ref_timestamps_.empty()) return;
    playing_ = true;
    last_advance_time_ = Clock::steady();

    if (on_state_changed) {
        auto cb = on_state_changed;
        event_bus_.post([cb]() { cb(true); });
    }
}

void SessionPlayer::pause() {
    playing_ = false;
    if (on_state_changed) {
        auto cb = on_state_changed;
        event_bus_.post([cb]() { cb(false); });
    }
}

void SessionPlayer::seekToFrame(int frame) {
    if (ref_timestamps_.empty()) return;
    current_frame_ = std::clamp(frame, 0, static_cast<int>(ref_timestamps_.size()) - 1);
    emitFrame(current_frame_, true);
}

void SessionPlayer::nextFrame() {
    if (current_frame_ < static_cast<int>(ref_timestamps_.size()) - 1) {
        current_frame_++;
        emitFrame(current_frame_, true);
    }
}

void SessionPlayer::prevFrame() {
    if (current_frame_ > 0) {
        current_frame_--;
        emitFrame(current_frame_, true);
    }
}

void SessionPlayer::setSpeed(float speed) {
    speed_ = speed;
}

void SessionPlayer::update(double current_time) {
    if (!playing_ || ref_timestamps_.empty()) return;

    // Check if it's time to advance
    int total = static_cast<int>(ref_timestamps_.size());
    if (current_frame_ >= total - 1) {
        pause();
        return;
    }

    double dt_ref = ref_timestamps_[current_frame_ + 1] - ref_timestamps_[current_frame_];
    double dt_wall = current_time - last_advance_time_;

    if (dt_wall >= dt_ref / speed_) {
        current_frame_++;
        last_advance_time_ = current_time;
        emitFrame(current_frame_);
    }
}

int SessionPlayer::findNearest(const std::vector<double>& timestamps, double target) const {
    if (timestamps.empty()) return -1;

    auto it = std::lower_bound(timestamps.begin(), timestamps.end(), target);

    if (it == timestamps.end()) return static_cast<int>(timestamps.size()) - 1;
    if (it == timestamps.begin()) return 0;

    auto prev = it - 1;
    return (std::abs(*it - target) < std::abs(*prev - target))
        ? static_cast<int>(it - timestamps.begin())
        : static_cast<int>(prev - timestamps.begin());
}

void SessionPlayer::emitFrame(int frame_idx, bool force) {
    if (ref_timestamps_.empty() || frame_idx < 0 ||
        frame_idx >= static_cast<int>(ref_timestamps_.size())) return;

    double ref_ts = ref_timestamps_[frame_idx];

    // Notify frame change
    if (on_frame_changed) {
        on_frame_changed(frame_idx, ref_ts);
    }

    // Lidar
    if (session_data_.has_lidar && on_lidar_xyz && pcap_reader_) {
        int idx = findNearest(session_data_.lidar_timestamps, ref_ts);
        if (idx >= 0 && (force || idx != last_emitted_lidar_)) {
            last_emitted_lidar_ = idx;
            std::vector<float> xyz;
            int num_points = 0;
            if (pcap_reader_->getPointCloud(idx, xyz, num_points)) {
                on_lidar_xyz(xyz, num_points);
            }
        }
    }

    // Camera
    if (session_data_.has_camera && on_camera_frame && camera_prefetch_) {
        int idx = findNearest(session_data_.camera_timestamps, ref_ts);
        if (idx >= 0 && (force || idx != last_emitted_camera_)) {
            camera_prefetch_->seekHint(idx);

            // Try exact frame, then search nearby cached frames
            CameraFrame cf;
            bool found = camera_prefetch_->getFrame(idx, cf);
            if (!found) {
                // Try a few nearby frames that might be cached
                for (int d = 1; d <= 3 && !found; ++d) {
                    if (camera_prefetch_->getFrame(idx - d, cf)) found = true;
                    else if (camera_prefetch_->getFrame(idx + d, cf)) found = true;
                }
            }

            if (found) {
                last_emitted_camera_ = idx;
                on_camera_frame(cf);
            }
        }
    }

    // Radar
    if (session_data_.has_radar && on_radar_tracks) {
        int idx = findNearest(session_data_.radar_timestamps, ref_ts);
        if (idx >= 0 && (force || idx != last_emitted_radar_)) {
            last_emitted_radar_ = idx;

            // Parse RDE scan from in-memory data
            const auto& rec = session_data_.radar_index[idx];
            std::vector<RadarTrack> tracks;

            if (rec.position >= 0 && rec.length > 0 &&
                static_cast<size_t>(rec.position + 4 + rec.length) <= session_data_.rde_data.size()) {
                // Skip 4-byte prefix + 8-byte scan_pattern + 16-byte header + 40-byte empty params = 68 bytes
                int64_t track_start = rec.position + 4 + 8 + 16 + 40;
                int64_t track_end = rec.position + 4 + rec.length;
                int num_tracks = static_cast<int>((track_end - track_start) / 8);

                for (int t = 0; t < num_tracks; ++t) {
                    int64_t offset = track_start + t * 8;
                    if (offset + 8 > static_cast<int64_t>(session_data_.rde_data.size())) break;

                    uint64_t packed = 0;
                    std::memcpy(&packed, session_data_.rde_data.data() + offset, 8);
                    tracks.push_back(RdeFormat::unpackTrack(packed));
                }
            }

            on_radar_tracks(tracks);
        }
    }

    // GPS
    if (session_data_.has_gps && on_gps_data) {
        int idx = findNearest(session_data_.gps_timestamps, ref_ts);
        if (idx >= 0 && (force || idx != last_emitted_gps_)) {
            last_emitted_gps_ = idx;
            GpsFix fix = GpsdFormat::unpack(session_data_.gps_records[idx]);
            on_gps_data(fix);
        }
    }

    // IMU
    if (session_data_.has_imu && on_imu_data) {
        int idx = findNearest(session_data_.imu_timestamps, ref_ts);
        if (idx >= 0 && (force || idx != last_emitted_imu_)) {
            last_emitted_imu_ = idx;
            ImuData imu = ImudFormat::unpack(session_data_.imu_records[idx]);
            on_imu_data(imu);
        }
    }

    // Lidar: handled by PcapReader (separate)
    // Camera: handled by CameraPrefetch (separate)
}

} // namespace msl
