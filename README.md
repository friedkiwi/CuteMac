# CuteMac

CuteMac is a Qt 6 based classic Macintosh emulator project. The long-term target set is 68000-era Macs, Macintosh IIcx, Macintosh Quadra 800, and Power Macintosh 6100, using original unmodified ROM images supplied by the user.

The repository is currently a scaffold for modular emulator work:

- reusable hardware devices such as SCSI, ADB, video, storage, and machine glue logic
- pluggable CPU cores for 68k and PowerPC targets
- machine profiles composed from reusable devices
- TOML configuration files for emulator and machine settings
- portable CMake/Qt 6 builds for Linux, Windows, macOS, and WebAssembly

The M68k CPU core starts from Musashi-derived source maintained directly in the CuteMac tree.

## Build

Ubuntu/WSL2 prerequisites:

```sh
sudo apt-get install build-essential cmake ninja-build qt6-base-dev qt6-tools-dev
sudo apt-get install libreadline-dev
```

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
```

The build produces `CuteMac` for profile management, `CuteMacSession` for emulator windows, `CuteMacDebugSession` for a readline debug console, and `CuteMacMacPlusSmoke` for headless ROM verification.

## Run

```sh
./build/CuteMac
```

`CuteMac` opens the profile manager. It stores TOML profiles in Qt's per-user app config location and uses Qt's per-user app data location for default ROM and disk-image folders. Starting a profile launches `CuteMacSession`, the individual emulator window.

Mac Plus ROM smoke test:

```sh
./build/CuteMacMacPlusSmoke work/roms/MacPlusV3.rom
```

Debug console:

```sh
./build/CuteMacDebugSession --profile ~/.config/friedkiwi/CuteMac/profiles/Mac_Plus.toml
```
