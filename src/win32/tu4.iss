; tu4.iss -- Inno Setup script for the tu4 (Ultima IV text-mode) Windows build.
;
; This packages the runnable distribution folder produced by
;   make -C src -f Makefile.mingw dist  U4PATH=<repo>/tu4-win32
; into a single Setup.exe installer.
;
; Unlike the old xu4 xu4.iss (which listed every asset by hand and shipped
; SDL.dll / SDL_mixer.dll for the pixel build), tu4.exe is a fully static
; SDL2 build: the ONLY runtime DLL is libxmp.dll (SDL2_mixer dlopen's it for
; .it music). We install the whole tu4-win32 tree recursively so the file set
; stays correct as assets are added/removed -- no per-file Source lines to
; maintain.
;
; tu4 ships only the graphics it cannot derive from Ultima IV. The derived
; assets (intro screens, codex/shrine screens, dungeon tiles, TITLE) are
; generated on first run by tu4-setup.exe from the user's own Ultima IV data.
; A post-install checkbox offers to run that step (it needs the Ultima IV data
; present, so it is unchecked by default).
;
; BUILD THE INSTALLER:
;   1) Build the dist folder (on Linux, cross-compile):
;        export PATH=~/mingw-tu4/llvm-mingw/bin:$PATH
;        make -C src -f Makefile.mingw dist \
;            CROSS=x86_64-w64-mingw32- \
;            SYSROOT=$HOME/mingw-tu4/sysroot/mingw64 \
;            U4PATH=$PWD/tu4-win32
;   2) Compile this script with Inno Setup's ISCC (on Windows, or via wine):
;        ISCC.exe src\win32\tu4.iss
;      By default it packages ..\..\tu4-win32 (relative to this script) and
;      writes the installer to <repo>/dist-win/tu4-<ver>-setup.exe.
;   Override paths on the command line if your layout differs, e.g.:
;        ISCC.exe /DDistDir="C:\path\to\tu4-win32" /DOutDir="C:\out" tu4.iss

#define AppName        "TU4"
#define AppVersion     "1.0"
#define AppPublisher   "ASCII Dragon / xu4 Team"
#define AppURL         "https://xu4.sourceforge.net/"
#define AppExeName     "tu4.exe"

; Where the built dist folder lives. Default: <repo>/tu4-win32, i.e. two levels
; up from this script (src/win32/ -> src/ -> repo/) then tu4-win32.
#ifndef DistDir
  #define DistDir "..\..\tu4-win32"
#endif

; Where to write the produced installer.
#ifndef OutDir
  #define OutDir "..\..\dist-win"
#endif

[Setup]
AppId={{8E3B6C10-4A2F-4E7D-9B1A-7C2F5D0A4B31}
AppName={#AppName}
AppVersion={#AppVersion}
AppVerName={#AppName} {#AppVersion}
AppPublisher={#AppPublisher}
AppPublisherURL={#AppURL}
AppSupportURL={#AppURL}
AppUpdatesURL={#AppURL}
DefaultDirName={autopf}\tu4
DefaultGroupName=tu4
AllowNoIcons=yes
; Per-user vs per-machine: prefer per-user so no admin is needed; fall back to
; asking. (lowest = allow non-admin install into the user profile.)
PrivilegesRequiredOverridesAllowed=dialog
OutputDir={#OutDir}
OutputBaseFilename=tu4-{#AppVersion}-setup
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
DisableProgramGroupPage=auto
; Version metadata (mirrors src/win32/tu4.rc).
VersionInfoVersion=1.0.0.0
VersionInfoProductVersion=1.0.0.0
VersionInfoCompany={#AppPublisher}
VersionInfoDescription=TU4 - Ultima IV Text Mode
VersionInfoCopyright=Copyright (c) 2026 ASCII Dragon; (c) 2002-2022 xu4 Team
; Use the embedded application icon for the installer's own shortcuts.
SetupIconFile=tu4.ico
; A license is shown during install.
LicenseFile={#DistDir}\COPYING.txt

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[Files]
; Install the ENTIRE dist tree recursively (exe + DLL + conf/ graphics/ mid/
; sound/ setup/ + docs). recursesubdirs + createallsubdirs keeps the layout.
; A user-supplied "ultima4" data folder (if they dropped one in during testing)
; is excluded so we never ship game data.
Source: "{#DistDir}\*"; DestDir: "{app}"; \
    Excludes: "ultima4\*,ultima4,*.zip"; \
    Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{group}\tu4";                 Filename: "{app}\{#AppExeName}"; WorkingDir: "{app}"; Comment: "Ultima IV, text mode"
Name: "{group}\Generate graphics (tu4-setup)"; Filename: "{app}\tu4-setup.exe"; Parameters: "--all"; WorkingDir: "{app}"; Comment: "Regenerate the derived EGA assets from your Ultima IV data"
Name: "{group}\README";              Filename: "{app}\README.txt";  WorkingDir: "{app}"; Flags: createonlyiffileexists
Name: "{group}\Setup instructions";  Filename: "{app}\setup\README.txt"; WorkingDir: "{app}"; Flags: createonlyiffileexists
Name: "{group}\License (COPYING)";   Filename: "{app}\COPYING.txt"; WorkingDir: "{app}"; Flags: createonlyiffileexists
Name: "{group}\{cm:UninstallProgram,tu4}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\tu4";           Filename: "{app}\{#AppExeName}"; WorkingDir: "{app}"; Tasks: desktopicon

[Run]
; Optional post-install: regenerate the derived graphics. Unchecked by default
; because it requires the user's Ultima IV data to be present next to tu4.exe
; (an "ultima4" folder or ultima4.zip). If it is not found, tu4-setup prints an
; instructional message and exits without harm.
Filename: "{app}\tu4-setup.exe"; \
    Parameters: "--all"; \
    WorkingDir: "{app}"; \
    Description: "Generate graphics now from Ultima IV data (requires an ""ultima4"" folder here)"; \
    Flags: postinstall skipifsilent unchecked
; Optional: launch tu4 after install.
Filename: "{app}\{#AppExeName}"; WorkingDir: "{app}"; \
    Description: "{cm:LaunchProgram,tu4}"; \
    Flags: nowait postinstall skipifsilent

[UninstallDelete]
; Assets regenerated by tu4-setup into graphics\EGA after install are not
; tracked by the installer; remove the whole install dir's generated tree so
; uninstall leaves nothing behind. (User settings/saves live in %APPDATA%\tu4
; and are intentionally preserved.)
Type: filesandordirs; Name: "{app}\graphics\EGA"
