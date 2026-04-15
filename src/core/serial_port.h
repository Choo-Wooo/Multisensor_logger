#pragma once

#include <string>
#include <vector>
#include <cstdint>

namespace msl {

/// Cross-platform serial port abstraction.
/// Windows: CreateFile + DCB + ReadFile
/// Linux: open + termios + read
class SerialPort {
public:
    SerialPort() = default;
    ~SerialPort();

    SerialPort(const SerialPort&) = delete;
    SerialPort& operator=(const SerialPort&) = delete;

    /// Open a serial port with the given baud rate.
    /// port: "COM3" on Windows, "/dev/ttyUSB0" on Linux
    bool open(const std::string& port, int baudrate);

    /// Close the serial port.
    void close();

    /// Check if the port is open.
    bool isOpen() const;

    /// Read up to maxBytes into buffer. Returns bytes actually read.
    /// timeout_ms: 0 = non-blocking, >0 = wait up to timeout_ms.
    int read(uint8_t* buf, int maxBytes, int timeout_ms = 100);

    /// Write bytes to the port. Returns bytes actually written.
    int write(const uint8_t* buf, int size);

    /// List available serial ports on the system.
    static std::vector<std::string> listPorts();

private:
#ifdef _WIN32
    void* handle_ = nullptr;  // HANDLE (void* to avoid windows.h in header)
#else
    int fd_ = -1;
#endif
    bool is_open_ = false;
};

} // namespace msl
