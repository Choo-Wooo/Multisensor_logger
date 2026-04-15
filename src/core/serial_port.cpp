#include "serial_port.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <setupapi.h>
#else
#include <fcntl.h>
#include <unistd.h>
#include <termios.h>
#include <sys/ioctl.h>
#include <dirent.h>
#include <cstring>
#endif

#include <algorithm>
#include <spdlog/spdlog.h>

namespace msl {

SerialPort::~SerialPort() {
    close();
}

#ifdef _WIN32
// ===================== Windows Implementation =====================

bool SerialPort::open(const std::string& port, int baudrate) {
    if (is_open_) close();

    // Prefix with \\.\\ for COM ports > COM9
    std::string fullPort = "\\\\.\\" + port;

    HANDLE h = CreateFileA(
        fullPort.c_str(),
        GENERIC_READ | GENERIC_WRITE,
        0, nullptr, OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL, nullptr
    );

    if (h == INVALID_HANDLE_VALUE) {
        spdlog::error("SerialPort: Failed to open {}", port);
        return false;
    }

    // Configure DCB
    DCB dcb{};
    dcb.DCBlength = sizeof(DCB);
    if (!GetCommState(h, &dcb)) {
        CloseHandle(h);
        return false;
    }

    dcb.BaudRate = baudrate;
    dcb.ByteSize = 8;
    dcb.Parity   = NOPARITY;
    dcb.StopBits = ONESTOPBIT;
    dcb.fDtrControl = DTR_CONTROL_ENABLE;
    dcb.fRtsControl = RTS_CONTROL_ENABLE;
    dcb.fBinary = TRUE;
    dcb.fParity = FALSE;

    if (!SetCommState(h, &dcb)) {
        CloseHandle(h);
        return false;
    }

    // Set timeouts (read timeout handled in read())
    COMMTIMEOUTS timeouts{};
    timeouts.ReadIntervalTimeout         = MAXDWORD;
    timeouts.ReadTotalTimeoutMultiplier  = 0;
    timeouts.ReadTotalTimeoutConstant    = 100; // default 100ms
    timeouts.WriteTotalTimeoutMultiplier = 0;
    timeouts.WriteTotalTimeoutConstant   = 1000;
    SetCommTimeouts(h, &timeouts);

    // Purge buffers
    PurgeComm(h, PURGE_RXCLEAR | PURGE_TXCLEAR);

    handle_ = h;
    is_open_ = true;
    spdlog::info("SerialPort: Opened {} @ {} baud", port, baudrate);
    return true;
}

void SerialPort::close() {
    if (is_open_ && handle_) {
        CloseHandle(static_cast<HANDLE>(handle_));
        handle_ = nullptr;
        is_open_ = false;
    }
}

bool SerialPort::isOpen() const {
    return is_open_;
}

int SerialPort::read(uint8_t* buf, int maxBytes, int timeout_ms) {
    if (!is_open_) return -1;

    HANDLE h = static_cast<HANDLE>(handle_);

    // Update read timeout
    COMMTIMEOUTS timeouts{};
    timeouts.ReadIntervalTimeout         = MAXDWORD;
    timeouts.ReadTotalTimeoutMultiplier  = 0;
    timeouts.ReadTotalTimeoutConstant    = timeout_ms;
    timeouts.WriteTotalTimeoutMultiplier = 0;
    timeouts.WriteTotalTimeoutConstant   = 1000;
    SetCommTimeouts(h, &timeouts);

    DWORD bytesRead = 0;
    if (!ReadFile(h, buf, maxBytes, &bytesRead, nullptr)) {
        return -1;
    }
    return static_cast<int>(bytesRead);
}

int SerialPort::write(const uint8_t* buf, int size) {
    if (!is_open_) return -1;

    HANDLE h = static_cast<HANDLE>(handle_);
    DWORD written = 0;
    if (!WriteFile(h, buf, size, &written, nullptr)) {
        return -1;
    }
    return static_cast<int>(written);
}

std::vector<std::string> SerialPort::listPorts() {
    std::vector<std::string> ports;
    for (int i = 1; i <= 256; ++i) {
        std::string port = "COM" + std::to_string(i);
        std::string fullPort = "\\\\.\\" + port;
        HANDLE h = CreateFileA(fullPort.c_str(), GENERIC_READ | GENERIC_WRITE,
                               0, nullptr, OPEN_EXISTING, 0, nullptr);
        if (h != INVALID_HANDLE_VALUE) {
            ports.push_back(port);
            CloseHandle(h);
        }
    }
    return ports;
}

#else
// ===================== Linux Implementation =====================

static speed_t baudToSpeed(int baud) {
    switch (baud) {
        case 4800:   return B4800;
        case 9600:   return B9600;
        case 19200:  return B19200;
        case 38400:  return B38400;
        case 57600:  return B57600;
        case 115200: return B115200;
        default:     return B9600;
    }
}

bool SerialPort::open(const std::string& port, int baudrate) {
    if (is_open_) close();

    fd_ = ::open(port.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (fd_ < 0) {
        spdlog::error("SerialPort: Failed to open {}", port);
        return false;
    }

    // Clear O_NONBLOCK for blocking reads with timeout
    int flags = fcntl(fd_, F_GETFL, 0);
    fcntl(fd_, F_SETFL, flags & ~O_NONBLOCK);

    struct termios tios{};
    tcgetattr(fd_, &tios);

    // Raw mode
    cfmakeraw(&tios);

    // 8N1
    tios.c_cflag &= ~(PARENB | CSTOPB | CSIZE);
    tios.c_cflag |= CS8 | CLOCAL | CREAD;

    // Baud rate
    speed_t spd = baudToSpeed(baudrate);
    cfsetispeed(&tios, spd);
    cfsetospeed(&tios, spd);

    // Timeout: 1 decisecond (0.1s) intervals, min 0 bytes
    tios.c_cc[VTIME] = 1;
    tios.c_cc[VMIN]  = 0;

    tcflush(fd_, TCIOFLUSH);
    tcsetattr(fd_, TCSANOW, &tios);

    is_open_ = true;
    spdlog::info("SerialPort: Opened {} @ {} baud", port, baudrate);
    return true;
}

void SerialPort::close() {
    if (is_open_ && fd_ >= 0) {
        ::close(fd_);
        fd_ = -1;
        is_open_ = false;
    }
}

bool SerialPort::isOpen() const {
    return is_open_;
}

int SerialPort::read(uint8_t* buf, int maxBytes, int timeout_ms) {
    if (!is_open_) return -1;

    // Use select() for timeout
    fd_set rfds;
    FD_ZERO(&rfds);
    FD_SET(fd_, &rfds);

    struct timeval tv;
    tv.tv_sec  = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;

    int ret = select(fd_ + 1, &rfds, nullptr, nullptr, &tv);
    if (ret <= 0) return 0;  // timeout or error

    ssize_t n = ::read(fd_, buf, maxBytes);
    return static_cast<int>(n);
}

int SerialPort::write(const uint8_t* buf, int size) {
    if (!is_open_) return -1;
    ssize_t n = ::write(fd_, buf, size);
    return static_cast<int>(n);
}

std::vector<std::string> SerialPort::listPorts() {
    std::vector<std::string> ports;
    DIR* dir = opendir("/dev");
    if (!dir) return ports;

    struct dirent* entry;
    while ((entry = readdir(dir)) != nullptr) {
        std::string name = entry->d_name;
        if (name.find("ttyUSB") == 0 || name.find("ttyACM") == 0 ||
            name.find("ttyS") == 0) {
            ports.push_back("/dev/" + name);
        }
    }
    closedir(dir);
    std::sort(ports.begin(), ports.end());
    return ports;
}

#endif

} // namespace msl
