#pragma once

#include <string>

namespace msl {

/// Cross-platform native file/folder dialog.
class FileDialog {
public:
    /// Open a native folder selection dialog.
    /// Returns selected folder path, or empty string if cancelled.
    static std::string selectFolder(const std::string& title = "Select Folder",
                                     const std::string& defaultPath = "");

    /// Open a native folder selection dialog for loading a session.
    /// Returns selected folder path, or empty string if cancelled.
    static std::string selectSessionFolder(const std::string& title = "Load Session",
                                            const std::string& defaultPath = "");

    /// Get the directory where the executable is located.
    static std::string getExecutableDir();
};

} // namespace msl
