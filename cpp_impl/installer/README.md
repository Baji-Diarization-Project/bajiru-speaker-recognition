# Linkjiru installer (Windows)

Builds a single `Linkjiru-Setup-<version>.exe` (Inno Setup) that installs, per-machine:

- **VST3** to `C:\Program Files\Common Files\VST3\Linkjiru.vst3` (every DAW scans this)
- **Standalone** to `C:\Program Files\Linkjiru\` + a Start Menu shortcut

It includes an uninstaller (Add/Remove Programs + a Start Menu "Uninstall Linkjiru"), which removes both, along with the bundled `onnxruntime.dll`, `DirectML.dll`, `runtime_model.onnx`, and the Visual C++ runtime.

## Prerequisites

1. **Inno Setup 6.3+** — https://jrsoftware.org/isdl.php (provides `ISCC.exe`).
2. **Build the plugin with the model present** so the DLLs + model are staged next to each binary:
   ```powershell
   cmake --build cmake-build-release --target BuildAll --config Release
   ```
   (`cpp_impl\onnx-model\runtime_model.onnx` must exist, or the build's post-step fails.)

## Build the installer

From `cpp_impl\installer`:

```powershell
.\build_installer.ps1
# or a custom CMake build dir:
.\build_installer.ps1 -BuildDir ..\build
```

The script stages the built Standalone + VST3 (checking the required DLLs + model are present) into `.\staging`, then compiles `Linkjiru.iss`. The result lands in `.\Output`.

## Notes

- Per-machine install needs admin (elevation is requested by the installer).
- `build_installer.ps1` bundles the Visual C++ runtime (VCRUNTIME140/MSVCP140) app-local, located from your VS redist (`VCToolsRedistDir` or `vswhere`), so it runs on machines without the VC++ redistributable installed.
- The setup is ~75 MB: `runtime_model.onnx` (~31 MB) plus the DLLs are embedded twice, once per binary.
- `staging\` and `Output\` are build products; keep them out of version control.
