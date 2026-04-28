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

// Modern Vista+ folder picker (Explorer-style).
// Falls back to legacy SHBrowseForFolder if IFileOpenDialog creation fails.

#include <shobjidl.h>   // IFileOpenDialog
#include <string>

#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "shell32.lib")

// Convert UTF-8 to wide for Win32 APIs
static std::wstring utf8ToWide(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    std::wstring w(n > 0 ? n - 1 : 0, L'\0');
    if (n > 0) MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, &w[0], n);
    return w;
}

static std::string wideToUtf8(LPCWSTR s) {
    if (!s) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, s, -1, nullptr, 0, nullptr, nullptr);
    std::string u(n > 0 ? n - 1 : 0, '\0');
    if (n > 0) WideCharToMultiByte(CP_UTF8, 0, s, -1, &u[0], n, nullptr, nullptr);
    return u;
}

// Legacy fallback (Windows pre-Vista or unusual configs)
static int CALLBACK BrowseCallbackProc(HWND hwnd, UINT uMsg, LPARAM, LPARAM lpData) {
    if (uMsg == BFFM_INITIALIZED && lpData) {
        SendMessageA(hwnd, BFFM_SETSELECTIONA, TRUE, lpData);
    }
    return 0;
}

static std::string browseFolderLegacy(const std::string& title, const std::string& defaultPath) {
    char path[MAX_PATH] = {0};
    BROWSEINFOA bi = {};
    bi.lpszTitle = title.c_str();
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
    if (!defaultPath.empty()) {
        bi.lpfn = BrowseCallbackProc;
        bi.lParam = reinterpret_cast<LPARAM>(defaultPath.c_str());
    }
    LPITEMIDLIST pidl = SHBrowseForFolderA(&bi);
    std::string result;
    if (pidl) {
        if (SHGetPathFromIDListA(pidl, path)) result = path;
        CoTaskMemFree(pidl);
    }
    return result;
}

static std::string browseFolder(const std::string& title, const std::string& defaultPath) {
    HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    bool com_inited = SUCCEEDED(hr) || hr == RPC_E_CHANGED_MODE;

    std::string result;

    IFileOpenDialog* pDialog = nullptr;
    HRESULT hrCreate = CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_ALL,
                                        IID_IFileOpenDialog,
                                        reinterpret_cast<void**>(&pDialog));

    if (SUCCEEDED(hrCreate) && pDialog) {
        // Configure as folder picker
        DWORD opts = 0;
        pDialog->GetOptions(&opts);
        pDialog->SetOptions(opts | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST);

        if (!title.empty()) {
            std::wstring wtitle = utf8ToWide(title);
            pDialog->SetTitle(wtitle.c_str());
        }

        // Initial folder
        if (!defaultPath.empty()) {
            std::wstring wpath = utf8ToWide(defaultPath);
            IShellItem* pItem = nullptr;
            HRESULT hrSi = SHCreateItemFromParsingName(wpath.c_str(), nullptr,
                                                       IID_IShellItem,
                                                       reinterpret_cast<void**>(&pItem));
            if (SUCCEEDED(hrSi) && pItem) {
                pDialog->SetFolder(pItem);
                pItem->Release();
            }
        }

        HRESULT hrShow = pDialog->Show(nullptr);
        if (SUCCEEDED(hrShow)) {
            IShellItem* pResult = nullptr;
            if (SUCCEEDED(pDialog->GetResult(&pResult)) && pResult) {
                LPWSTR pszPath = nullptr;
                if (SUCCEEDED(pResult->GetDisplayName(SIGDN_FILESYSPATH, &pszPath)) && pszPath) {
                    result = wideToUtf8(pszPath);
                    CoTaskMemFree(pszPath);
                }
                pResult->Release();
            }
        }
        // hrShow == HRESULT_FROM_WIN32(ERROR_CANCELLED) means user cancelled.

        pDialog->Release();
    } else {
        // Fall back to old XP-style if IFileOpenDialog isn't available
        result = browseFolderLegacy(title, defaultPath);
    }

    if (com_inited) CoUninitialize();
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
