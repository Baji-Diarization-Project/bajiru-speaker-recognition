; Inno Setup script for Linkjiru (Windows x64, per-machine install).
; Requires Inno Setup 6.3+ (https://jrsoftware.org/isdl.php).
;
; Don't run ISCC on this directly unless you've staged the binaries first.
; Use build_installer.ps1; it stages the built Standalone + VST3 (with their
; onnxruntime.dll, DirectML.dll, runtime_model.onnx) into .\staging, then compiles
; this. Sourcing from .\staging keeps this script out of the CMake build tree.

#define MyAppName "Linkjiru"
#define MyAppVersion "0.1.0"
#define MyAppPublisher "tomobaji"
#define MyAppExeName "Linkjiru.exe"

[Setup]
AppId={{A7F3C2E1-5B4D-4E8A-9C1F-2D3E4F5A6B7C}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppPublisher={#MyAppPublisher}
DefaultDirName={autopf}\{#MyAppName}
DefaultGroupName={#MyAppName}
DisableProgramGroupPage=yes
; Per-machine: Program Files + Common Files\VST3 need elevation.
PrivilegesRequired=admin
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
OutputDir=Output
OutputBaseFilename=Linkjiru-Setup-{#MyAppVersion}
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
UninstallDisplayIcon={app}\{#MyAppExeName}
UninstallDisplayName={#MyAppName} {#MyAppVersion}

[Types]
Name: "full"; Description: "Full (VST3 + Standalone)"
Name: "custom"; Description: "Custom"; Flags: iscustom

[Components]
Name: "vst3"; Description: "VST3 plugin (for DAWs)"; Types: full custom
Name: "standalone"; Description: "Standalone app"; Types: full custom

[Files]
; Standalone app, alongside its onnxruntime.dll, DirectML.dll, runtime_model.onnx.
Source: "staging\Standalone\*"; DestDir: "{app}"; Components: standalone; \
    Flags: recursesubdirs createallsubdirs ignoreversion
; VST3 bundle -> the machine VST3 folder every DAW scans. The bundle already
; carries the same DLLs + model inside Contents\x86_64-win.
Source: "staging\VST3\Linkjiru.vst3\*"; DestDir: "{commoncf64}\VST3\Linkjiru.vst3"; Components: vst3; \
    Flags: recursesubdirs createallsubdirs ignoreversion uninsremovereadonly

[Icons]
Name: "{group}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; Components: standalone
Name: "{group}\Uninstall {#MyAppName}"; Filename: "{uninstallexe}"

[Run]
Filename: "{app}\{#MyAppExeName}"; Description: "Launch {#MyAppName}"; Components: standalone; \
    Flags: nowait postinstall skipifsilent
