#pragma once

#include "sensor_worker.h"
#include "core/sensor_data.h"
#include "core/serial_port.h"
#include <functional>
#include <string>

namespace msl {

/// GPS sensor worker: serial port reading + libnmea NMEA parsing.
class GpsWorker : public ISensorWorker {
public:
    GpsWorker(EventBus& bus, const std::string& port, int baudrate)
        : ISensorWorker(bus), port_name_(port), baudrate_(baudrate) {}

    /// Callback for parsed GPS fix (posted to EventBus).
    std::function<void(const GpsFix&)> on_fix_ready;

protected:
    bool onConnect() override;
    void pollLoop() override;
    void onDisconnect() override;

private:
    std::string port_name_;
    int baudrate_;
    SerialPort serial_;
    GpsFix current_fix_;

    /// Auto-detect serial port if port_name_ is empty.
    bool autoDetectPort();

    /// Auto-detect baud rate by trying common rates.
    bool autoDetectBaud();

    /// Process a complete NMEA sentence line.
    void processNmeaSentence(char* sentence, size_t len);
};

} // namespace msl
