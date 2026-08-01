# CuteMac

CuteMac is a Qt 6 based classic Macintosh emulator project. The long-term target set is 68000-era Macs, Macintosh SE/30, Macintosh Quadra 800, and Power Macintosh 6100, using original unmodified ROM images supplied by the user.

The repository is currently a scaffold for modular emulator work:

- reusable hardware devices such as SCSI, ADB, video, storage, and machine glue logic
- pluggable CPU cores for 68k and PowerPC targets
- machine profiles composed from reusable devices
- TOML configuration files for emulator and machine settings
- portable CMake/Qt 6 builds for Linux, Windows, macOS, and WebAssembly

## Build

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
```

The current executable is only a Qt 6 stub used to verify project structure and toolchain setup.
