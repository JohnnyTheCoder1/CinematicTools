## Alien: Isolation Cinematic Tools

This is a continuation of the Alien: Isolation Cinematic Tools by [Hattiwatti](https://github.com/Hattiwatti), with the intention of adding extra useful features for modding the game.

### How to build

The project must be built in release mode, for x86 systems. This is the default project configuration.

Use Visual Studio 2022 (MSVC v143 toolset) to compile, and make sure to restore the required NuGet packages (Boost and DirectXTK). MinHook is vendored under `Alien Isolation/ThirdParty/MinHook`, so no separate package restore is needed for it.

Alternatively, a GitHub Actions workflow builds Release binaries on Windows and publishes artifacts on every PR to master. See `.github/workflows/windows-build.yml`.

Artifacts include the built `CT_AlienIsolation.dll` for Win32 and (if configured) x64.

### How to use

To hook the cinematic tools into Alien: Isolation, run the game, and then launch "inject.bat" in the root of the project.

This utilises [Injector](https://github.com/nefarius/Injector) - a project by [Benjamin Höglinger](https://github.com/nefarius) (thanks!).

### Testing and injection tips

- Start Alien: Isolation and wait at least until the main menu (or load into a level) before injecting.
- Use the provided `Build/inject.bat` or your preferred injector targeting `AI.exe`.
- After injection, a log file is created at `Cinematic Tools/CT.log`. Check it for initialization steps, warnings, or errors.
- If injection fails safely, the tools will log the reason (e.g., invalid offsets, swapchain not ready) and exit without crashing the game.

### Troubleshooting

- If the game version differs from the hardcoded offsets, the tools will skip dangerous operations and log which offsets look invalid. Please share `Cinematic Tools/CT.log` so offsets can be updated or replaced with signature scans.
- If you see linking issues for `GetModuleInformation`, ensure `Psapi.lib` is linked (already handled in source by `#pragma comment(lib, "Psapi.lib")`).