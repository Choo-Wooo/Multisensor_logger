#include "file_dialog.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shlobj.h>
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "shell32.lib")
#else
#include <cstdio>
#include <cstdlib>
#include <array>
#include <unistd.h>
#endif

namespace msl {

#ifdef _WIN32

// Callback to set initial folder in browse dialog
static int CALLBACK BrowseCallbackProc(HWND hwnd, UINT uMsg, LPARAM, LPARAM lpData) {
    if (uMsg == BFFM_INITIALIZED && lpData) {
        SendMessageA(hwnd, BFFM_SETSELECTIONA, TRUE, lpData);
    }
    return 0;
}

static std::string browseFolder(const std::string& title, const std::string& defaultPath) {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    char path[MAX_PATH] = {0};

    BROWSEINFOA bi = {};
    bi.lpszTitle = title.c_str();
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;

    // Set initial folder if provided
    if (!defaultPath.empty()) {
        bi.lpfn = BrowseCallbackProc;
        bi.lParam = reinterpret_cast<LPARAM>(defaultPath.c_str());
    }

    LPITEMIDLIST pidl = SHBrowseForFolderA(&bi);
    std::string result;

    if (pidl) {
        if (SHGetPathFromIDListA(pidl, path)) {
            result = path;
        }
        CoTaskMemFree(pidl);
    }

    CoUninitialize();
    return result;
}

std::string FileDialog::selectFolder(const std::string& title, const std::string& defaultPath) {
    return browseFolder(title, defaultPath);
}

std::string FileDialog::selectSessionFolder(const std::string& title, const std::string& defaultPath) {
    return browseFolder(title, defaultPath);
}

std::string FileDialog::getExecutableDir() {
    char path[MAX_PATH] = {0};
    GetModuleFileNameA(nullptr, path, MAX_PATH);
    std::string fullPath = path;
    auto pos = fullPath.find_last_of("\\/");
    if (pos != std::string::npos)
        return fullPath.substr(0, pos);
    return ".";
}

#else

static std::string runCommand(const std::string& cmd) {
    std::array<char, 1024> buffer;
    std::string result;
    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) return "";
    while (fgets(buffer.data(), buffer.size(), pipe) != nullptr) {
        result += buffer.data();
    }
    pclose(pipe);
    while (!result.empty() && (result.back() == '\n' || result.back() == '\r'))
        result.pop_back();
    return result;
}

std::string FileDialog::selectFolder(const std::string& title, const std::string& defaultPath) {
    std::string pathArg;
    if (!defaultPath.empty()) {
        pathArg = " --filename=\"" + defaultPath + "/\"";
    }
    std::string result = runCommand("zenity --file-selection --directory --title=\"" + title + "\"" + pathArg + " 2>/dev/null");
    if (!result.empty()) return result;
    // Fallback
    std::string kdArg = defaultPath.empty() ? "." : defaultPath;
    result = runCommand("kdialog --getexistingdirectory \"" + kdArg + "\" --title \"" + title + "\" 2>/dev/null");
    return result;
}

std::string FileDialog::selectSessionFolder(const std::string& title, const std::string& defaultPath) {
    return selectFolder(title, defaultPath);
}

std::string FileDialog::getExecutableDir() {
    char buf[1024] = {0};
    ssize_t len = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (len > 0) {
        buf[len] = '\0';
        std::string fullPath = buf;
        auto pos = fullPath.find_last_of('/');
        if (pos != std::string::npos)
            return fullPath.substr(0, pos);
    }
    return ".";
}

#endif

} // namespace msl
