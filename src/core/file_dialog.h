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

    /// User's Documents folder (Windows: %USERPROFILE%\Documents,
    /// Linux: ~/Documents). Falls back to home directory if missing.
    static std::string getUserDocumentsDir();

    /// Per-user app data directory for this application
    /// (Windows: %APPDATA%\MultisensorLogger,
    ///  Linux:   ~/.config/MultisensorLogger).
    /// The directory is created if it doesn't exist.
    static std::string getAppDataDir();

    /// Default location for recordings. Created if missing.
    /// Windows: %USERPROFILE%\Videos\MultisensorLogger
    /// Linux:   ~/Videos/MultisensorLogger
    static std::string getDefaultDataDir();

    /// Path to user's config.ini (under getAppDataDir()).
    static std::string getConfigPath();
};

} // namespace msl
