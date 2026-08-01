# CuteMac

CuteMac is a Qt 6 based classic Macintosh emulator project. The long-term target set is 68000-era Macs, Macintosh IIcx, Macintosh Quadra 800, and Power Macintosh 6100, using original unmodified ROM images supplied by the user.

The repository is currently early emulator bringup work:

- reusable hardware devices such as SCSI, ADB, video, storage, and machine glue logic
- Mac Plus IWM/floppy image loading for raw 400K/800K and Disk Copy 4.2 media
- pluggable CPU cores for 68k and PowerPC targets
- machine profiles composed from reusable devices
- Macintosh IIcx boot support for System 6 raw 800K floppies, NuBus, ADB, SWIM1, NCR5380 SCSI, ASC, and dual VIAs
- NuBus video using configurable CuteMac Video or the authentic Apple Macintosh II Video Card (630-0153)
- TOML configuration files for emulator and machine settings
- portable CMake/Qt 6 builds for Linux, Windows, macOS, and WebAssembly

The M68k CPU core starts from Musashi-derived source maintained directly in the CuteMac tree.

## Build

Ubuntu/WSL2 prerequisites:

```sh
sudo apt-get install build-essential cmake ninja-build qt6-base-dev qt6-tools-dev
sudo apt-get install libreadline-dev libtomlplusplus-dev
```

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
```

The build produces `CuteMac` for profile management, `CuteMacSession` for emulator windows, `CuteMacDebugSession` for a readline debug console, and headless Mac Plus/IIcx smoke tools.

Every push also runs the Windows Win64 workflow. Its `CuteMac-win64` artifact is a portable folder containing the release executables and their Qt 6 runtime dependencies.

`CuteMacSession` and `CuteMacDebugSession` share the machine-neutral `EmulationSession` facade. Desktop sessions run emulation off the Qt event thread; WebAssembly uses a single-threaded frame runner. Debug-only concrete machine access is kept behind an optional debug interface.

## Run

```sh
./build/CuteMac
```

`CuteMac` opens the profile manager. It stores TOML profiles in Qt's per-user app config location and uses Qt's per-user app data location for default ROM and disk-image folders. Starting a profile launches `CuteMacSession`, the individual emulator window.

Optional, ROM-version-verified patches can be enabled per profile. For example, the supported Mac Plus v3 ROM can skip its destructive RAM pattern passes while retaining ROM verification and RAM sizing:

```toml
[rom_patches]
skip_ram_pattern_test = true
```

Runtime speed can be changed live from the session's Machine menu and persisted in a profile:

```toml
[runtime]
speed = "realtime" # or "unlimited"
```

Mac Plus profiles can configure a 256-byte NVRAM image. RTC reads expose the current host-local date/time in the classic Macintosh epoch; guest time writes are intentionally discarded, while PRAM changes persist to this image:

```toml
[storage]
nvram_path = "/path/to/mac-plus.nvram"
```

New profiles default to unlimited speed. The shared machine configuration window provides capability-dependent general, floppy, SCSI, and NuBus tabs. The authentic 630-0153 card requires its user-supplied 4 KiB `342-0008-A` declaration ROM. The Tools menu provides a disk image manager for typed floppy and hard-disk images.

Mac Plus ROM smoke test:

```sh
./build/CuteMacMacPlusSmoke work/roms/MacPlusV3.rom
```

Debug console:

```sh
./build/CuteMacDebugSession --profile ~/.config/friedkiwi/CuteMac/profiles/Mac_Plus.toml
```

Useful debug commands include `regs`, `disasm pc 8`, `mem <addr> <len>`, `step`, `run`, `devices scsi`, `devices iwm`, `devices via`, `disk insert <path>`, `floppy insert <path>`, `mouse status`, `key status`, `screen export <file.png>`, `sound capture-export <file.wav>`, and `gdb start`.
For GDB, use `gdb-multiarch` with `set architecture m68k:68000`, `set endian big`, and `target remote localhost:1234`.
