; Multi-Sensor Logger Installer Script
; NSIS Script for Windows Installer
;
; Usage: makensis installer.nsi
; Or build via build_installer.bat (handles the Release build + staging too).

;--------------------------------
; Include Modern UI
!include "MUI2.nsh"

;--------------------------------
; Application Settings
!define APP_NAME       "Multi-Sensor Logger"
!define APP_VERSION    "2.0.1"
!define COMPANY_NAME   ""
!define APP_EXE        "MultisensorLogger.exe"

; Project root and build artifact directory. The .bat passes both as absolute
; paths via /D defines. Default fallbacks assume installer.nsi lives in
; <project>/build_install/.
!ifndef PROJECT_ROOT
    !define PROJECT_ROOT ".."
!endif
!ifndef STAGE_DIR
    !define STAGE_DIR    "${PROJECT_ROOT}\cmake-build-release-visual-studio\bin\Release"
!endif

; Output installer name
!ifndef OUTPUT_NAME
    !define OUTPUT_NAME "MultisensorLogger_Setup_v${APP_VERSION}.exe"
!endif

;--------------------------------
; General Configuration

Name "${APP_NAME}"
OutFile "${OUTPUT_NAME}"

; Default installation folder. The Windows build is 32-bit (all 3rdparty DLLs are x86).
InstallDir "$PROGRAMFILES32\${APP_NAME}"

; Get installation folder from registry if available
InstallDirRegKey HKLM "Software\${APP_NAME}" "Install_Dir"

; Request admin privileges (writing to Program Files + registry)
RequestExecutionLevel admin

; Compression - solid LZMA gives the smallest setup
SetCompressor /SOLID lzma

;--------------------------------
; Interface Settings

!define MUI_ABORTWARNING

; Use the application icon if it's available, otherwise fall back to NSIS default.
!define APP_ICON "${PROJECT_ROOT}\assets\icons\icon.ico"
!define MUI_ICON   "${APP_ICON}"
!define MUI_UNICON "${APP_ICON}"

; Optional: shown on the finish page
!define MUI_FINISHPAGE_RUN "$INSTDIR\${APP_EXE}"
!define MUI_FINISHPAGE_RUN_TEXT "Launch ${APP_NAME}"

;--------------------------------
; Pages

!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH

!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES

;--------------------------------
; Languages

!insertmacro MUI_LANGUAGE "Korean"
!insertmacro MUI_LANGUAGE "English"

;--------------------------------
; Version Information

VIProductVersion "2.0.1.0"
VIAddVersionKey "ProductName"     "${APP_NAME}"
VIAddVersionKey "CompanyName"     "${COMPANY_NAME}"
VIAddVersionKey "FileDescription" "${APP_NAME} Installer"
VIAddVersionKey "FileVersion"     "${APP_VERSION}"
VIAddVersionKey "ProductVersion"  "${APP_VERSION}"
VIAddVersionKey "LegalCopyright"  ""

;--------------------------------
; Main install section

Section "MainSection" SEC01

    SetOutPath "$INSTDIR"

    ; --- Executable ---
    File "${STAGE_DIR}\${APP_EXE}"

    ; --- Application DLLs ---
    ; Radar SDKs
    File "${STAGE_DIR}\BSR20_SDK.dll"
    File "${STAGE_DIR}\BSR30_SDK.dll"
    File "${STAGE_DIR}\nmea.dll"
    File "${STAGE_DIR}\uv.dll"

    ; UI / windowing
    File "${STAGE_DIR}\imgui.dll"
    File "${STAGE_DIR}\glfw3.dll"

    ; FFmpeg / camera capture stack
    File "${STAGE_DIR}\avcodec-62.dll"
    File "${STAGE_DIR}\avformat-62.dll"
    File "${STAGE_DIR}\avutil-60.dll"
    File "${STAGE_DIR}\swscale-9.dll"
    File "${STAGE_DIR}\libx264-165.dll"
    File "${STAGE_DIR}\ccap.dll"

    ; OpenSSL / compression / runtime support
    File "${STAGE_DIR}\libcrypto-3.dll"
    File "${STAGE_DIR}\libssl-3.dll"
    File "${STAGE_DIR}\libbz2-1.dll"
    File "${STAGE_DIR}\zlib1.dll"
    File "${STAGE_DIR}\libiconv-2.dll"
    File "${STAGE_DIR}\libgcc_s_dw2-1.dll"
    File "${STAGE_DIR}\libwinpthread-1.dll"

    ; VAAPI shims that ffmpeg pulls in on Windows
    File /nonfatal "${STAGE_DIR}\libva.dll"
    File /nonfatal "${STAGE_DIR}\libva_win32.dll"

    ; Misc support
    File /nonfatal "${STAGE_DIR}\shared_library.dll"

    ; --- VC++ runtime, app-local (x86). Lets the app start even if the redist
    ;     section is skipped or fails. UCRT (api-ms-win-crt-*) is part of Windows 10+.
    File "${STAGE_DIR}\msvcp140.dll"
    File "${STAGE_DIR}\vcruntime140.dll"
    File /nonfatal "${STAGE_DIR}\msvcp140_atomic_wait.dll"
    File /nonfatal "${STAGE_DIR}\msvcp140_codecvt_ids.dll"

    ; --- Default config ---
    ; Don't overwrite an existing user-edited config on upgrade.
    SetOverwrite ifnewer
    File "${PROJECT_ROOT}\config.ini"
    SetOverwrite on

    ; --- Assets (icons, etc.) ---
    SetOutPath "$INSTDIR\assets"
    File /r "${PROJECT_ROOT}\assets\*.*"
    SetOutPath "$INSTDIR"

    ; --- Empty Data folder so the app has somewhere to write ---
    CreateDirectory "$INSTDIR\Data"
    CreateDirectory "$INSTDIR\logs"

    ; --- Registry keys for install dir + Add/Remove Programs ---
    WriteRegStr HKLM "Software\${APP_NAME}" "Install_Dir" "$INSTDIR"
    WriteRegStr HKLM "Software\${APP_NAME}" "Version"     "${APP_VERSION}"

    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${APP_NAME}" \
        "DisplayName"     "${APP_NAME}"
    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${APP_NAME}" \
        "DisplayVersion"  "${APP_VERSION}"
    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${APP_NAME}" \
        "DisplayIcon"     '"$INSTDIR\${APP_EXE}"'
    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${APP_NAME}" \
        "Publisher"       "${COMPANY_NAME}"
    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${APP_NAME}" \
        "InstallLocation" "$INSTDIR"
    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${APP_NAME}" \
        "UninstallString" '"$INSTDIR\uninstall.exe"'
    WriteRegDWORD HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${APP_NAME}" \
        "NoModify" 1
    WriteRegDWORD HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${APP_NAME}" \
        "NoRepair" 1

    ; --- Uninstaller ---
    WriteUninstaller "$INSTDIR\uninstall.exe"

SectionEnd

;--------------------------------
; Optional: VC++ Redistributable (only if it's been placed next to installer.nsi)

; The app is 32-bit, so it needs the x86 runtime (vc_redist.x86.exe), not x64.
; Bundled only if the file exists next to installer.nsi at compile time.
Section "Visual C++ Redistributable (x86)" SEC_VC

!if /FileExists "VC_redist.x86.exe"
    SetOutPath "$TEMP"
    File "VC_redist.x86.exe"
    DetailPrint "Installing Visual C++ Redistributable (x86)..."
    ExecWait '"$TEMP\VC_redist.x86.exe" /install /passive /norestart' $0
    DetailPrint "VC++ x86 installer returned: $0"
    Delete "$TEMP\VC_redist.x86.exe"
!else
    DetailPrint "VC_redist.x86.exe not bundled; install it manually if the app fails to start:"
    DetailPrint "  https://aka.ms/vs/17/release/vc_redist.x86.exe"
!endif

SectionEnd

;--------------------------------
; Start Menu Shortcuts

Section "Start Menu Shortcuts" SEC02

    CreateDirectory "$SMPROGRAMS\${APP_NAME}"
    SetOutPath "$INSTDIR"
    CreateShortcut "$SMPROGRAMS\${APP_NAME}\${APP_NAME}.lnk" \
        "$INSTDIR\${APP_EXE}" "" "$INSTDIR\${APP_EXE}" 0
    CreateShortcut "$SMPROGRAMS\${APP_NAME}\Uninstall.lnk" \
        "$INSTDIR\uninstall.exe"

SectionEnd

;--------------------------------
; Desktop Shortcut

Section "Desktop Shortcut" SEC03

    SetOutPath "$INSTDIR"
    CreateShortcut "$DESKTOP\${APP_NAME}.lnk" \
        "$INSTDIR\${APP_EXE}" "" "$INSTDIR\${APP_EXE}" 0

SectionEnd

;--------------------------------
; Uninstaller

Section "Uninstall"

    ; --- Files ---
    Delete "$INSTDIR\${APP_EXE}"
    Delete "$INSTDIR\BSR20_SDK.dll"
    Delete "$INSTDIR\BSR30_SDK.dll"
    Delete "$INSTDIR\nmea.dll"
    Delete "$INSTDIR\uv.dll"
    Delete "$INSTDIR\imgui.dll"
    Delete "$INSTDIR\glfw3.dll"
    Delete "$INSTDIR\avcodec-62.dll"
    Delete "$INSTDIR\avformat-62.dll"
    Delete "$INSTDIR\avutil-60.dll"
    Delete "$INSTDIR\swscale-9.dll"
    Delete "$INSTDIR\libx264-165.dll"
    Delete "$INSTDIR\ccap.dll"
    Delete "$INSTDIR\libcrypto-3.dll"
    Delete "$INSTDIR\libssl-3.dll"
    Delete "$INSTDIR\libbz2-1.dll"
    Delete "$INSTDIR\zlib1.dll"
    Delete "$INSTDIR\libiconv-2.dll"
    Delete "$INSTDIR\libgcc_s_dw2-1.dll"
    Delete "$INSTDIR\libwinpthread-1.dll"
    Delete "$INSTDIR\libva.dll"
    Delete "$INSTDIR\libva_win32.dll"
    Delete "$INSTDIR\shared_library.dll"
    Delete "$INSTDIR\config.ini"
    Delete "$INSTDIR\uninstall.exe"

    ; --- Folders ---
    RMDir /r "$INSTDIR\assets"
    RMDir /r "$INSTDIR\logs"
    ; Leave Data/ alone - it may contain user recordings the user wants to keep.
    ; RMDir /r "$INSTDIR\Data"
    RMDir "$INSTDIR"

    ; --- Shortcuts ---
    Delete "$SMPROGRAMS\${APP_NAME}\*.*"
    RMDir  "$SMPROGRAMS\${APP_NAME}"
    Delete "$DESKTOP\${APP_NAME}.lnk"

    ; --- Registry ---
    DeleteRegKey HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${APP_NAME}"
    DeleteRegKey HKLM "Software\${APP_NAME}"

SectionEnd
