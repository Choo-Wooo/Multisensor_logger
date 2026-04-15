#include "gps_worker.h"

#define NMEA_DLL
#include <nmea.h>
#include <nmea/gpgga.h>
#include <nmea/gprmc.h>
#include <nmea/gpgsa.h>
#include <nmea/gpvtg.h>

#include <spdlog/spdlog.h>
#include <cstring>
#include <cmath>

namespace msl {

bool GpsWorker::onConnect() {
    // Auto-detect port if empty
    if (port_name_.empty()) {
        if (!autoDetectPort()) {
            notifyError("GPS: No serial port found");
            return false;
        }
    }

    // Try configured baud rate first, then auto-detect
    if (serial_.open(port_name_, baudrate_)) {
        spdlog::info("GPS: Connected to {} @ {}", port_name_, baudrate_);
        return true;
    }

    // Auto-detect baud rate
    if (autoDetectBaud()) {
        return true;
    }

    notifyError("GPS: Failed to open " + port_name_);
    return false;
}

void GpsWorker::pollLoop() {
    char line_buf[256];
    int line_pos = 0;

    while (running_ && !disconnect_requested_) {
        uint8_t c;
        int n = serial_.read(&c, 1, 100);
        if (n <= 0) continue;

        if (c == '\n') {
            // Remove trailing \r if present
            if (line_pos > 0 && line_buf[line_pos - 1] == '\r')
                line_pos--;

            line_buf[line_pos] = '\0';

            if (line_pos > 6 && line_buf[0] == '$') {
                // Re-add \r\n for nmea_parse (it expects standard line endings)
                line_buf[line_pos++] = '\r';
                line_buf[line_pos++] = '\n';
                line_buf[line_pos] = '\0';
                processNmeaSentence(line_buf, line_pos);
            }
            line_pos = 0;
        } else if (line_pos < 254) {
            line_buf[line_pos++] = static_cast<char>(c);
        }
    }
}

void GpsWorker::onDisconnect() {
    serial_.close();
    spdlog::info("GPS: Disconnected");
}

void GpsWorker::processNmeaSentence(char* sentence, size_t len) {
    nmea_s* data = nmea_parse(sentence, len, 1);
    if (!data) return;

    switch (data->type) {
        case NMEA_GPGGA: {
            auto* gga = reinterpret_cast<nmea_gpgga_s*>(data);
            // Convert position: degrees + minutes/60
            current_fix_.latitude = gga->latitude.degrees + gga->latitude.minutes / 60.0;
            if (gga->latitude.cardinal == 'S')
                current_fix_.latitude = -current_fix_.latitude;

            current_fix_.longitude = gga->longitude.degrees + gga->longitude.minutes / 60.0;
            if (gga->longitude.cardinal == 'W')
                current_fix_.longitude = -current_fix_.longitude;

            current_fix_.altitude    = static_cast<float>(gga->altitude);
            current_fix_.fix_quality = static_cast<uint8_t>(gga->position_fix);
            current_fix_.satellites  = static_cast<uint8_t>(gga->n_satellites);
            current_fix_.valid       = (gga->position_fix > 0);

            // Emit fix on GGA (primary sentence)
            if (recording_) {
                current_fix_.pc_ts_rel = getRelativeTime();
            }

            frame_count_++;

            if (on_fix_ready) {
                GpsFix fix_copy = current_fix_;
                auto cb = on_fix_ready;
                event_bus_.post([cb, fix_copy]() { cb(fix_copy); });
            }
            break;
        }

        case NMEA_GPRMC: {
            auto* rmc = reinterpret_cast<nmea_gprmc_s*>(data);
            current_fix_.speed_kmh = static_cast<float>(rmc->gndspd_knots * 1.852);
            current_fix_.heading   = static_cast<float>(rmc->track_deg);
            current_fix_.valid     = rmc->valid;

            // Compute Unix timestamp from date_time
            current_fix_.timestamp = static_cast<double>(mktime(&rmc->date_time));
            break;
        }

        case NMEA_GPGSA: {
            auto* gsa = reinterpret_cast<nmea_gpgsa_s*>(data);
            current_fix_.pdop = static_cast<float>(gsa->pdop);
            current_fix_.hdop = static_cast<float>(gsa->hdop);
            current_fix_.vdop = static_cast<float>(gsa->vdop);
            break;
        }

        case NMEA_GPVTG: {
            auto* vtg = reinterpret_cast<nmea_gpvtg_s*>(data);
            if (vtg->gndspd_kmph > 0)
                current_fix_.speed_kmh = static_cast<float>(vtg->gndspd_kmph);
            if (vtg->track_deg > 0)
                current_fix_.heading = static_cast<float>(vtg->track_deg);
            break;
        }

        default:
            break;
    }

    nmea_free(data);
}

bool GpsWorker::autoDetectPort() {
    auto ports = SerialPort::listPorts();
    if (ports.empty()) return false;

    // Try each port, look for NMEA data
    for (const auto& port : ports) {
        SerialPort test;
        if (!test.open(port, baudrate_)) continue;

        uint8_t buf[256];
        int n = test.read(buf, sizeof(buf), 2000);
        test.close();

        if (n > 0) {
            // Check for '$' (NMEA start)
            for (int i = 0; i < n; ++i) {
                if (buf[i] == '$') {
                    port_name_ = port;
                    spdlog::info("GPS: Auto-detected port {}", port);
                    return true;
                }
            }
        }
    }
    return false;
}

bool GpsWorker::autoDetectBaud() {
    const int bauds[] = {9600, 115200, 38400, 57600, 4800, 19200};

    for (int baud : bauds) {
        serial_.close();
        if (!serial_.open(port_name_, baud)) continue;

        uint8_t buf[256];
        int n = serial_.read(buf, sizeof(buf), 2000);
        if (n > 0) {
            for (int i = 0; i < n; ++i) {
                if (buf[i] == '$') {
                    baudrate_ = baud;
                    spdlog::info("GPS: Auto-detected baud rate {}", baud);
                    return true;
                }
            }
        }
        serial_.close();
    }
    return false;
}

} // namespace msl
