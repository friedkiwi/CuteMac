# CuteMac

CuteMac is a Qt 6 based classic Macintosh emulator project. The long-term target set is 68000-era Macs, Macintosh IIcx, Macintosh Quadra 800, and the launch-model Power Macintosh 8100/80, using original unmodified ROM images supplied by the user.

The repository is currently early emulator bringup work:

- reusable hardware devices such as SCSI, ADB, video, storage, and machine glue logic
- Mac Plus IWM/floppy image loading for raw 400K/800K and Disk Copy 4.2 media
- pluggable CPU cores for 68k and PowerPC targets
- machine profiles composed from reusable devices
- Macintosh IIcx boot support for System 6/7 raw 800K GCR and 1.44 MB MFM floppies, NuBus, ADB, SWIM1, NCR5380 SCSI, ASC, and dual VIAs
- NuBus video using configurable CuteMac Video or the authentic Apple Macintosh II Video Card (630-0153)
- TOML configuration files for emulator and machine settings
- portable CMake/Qt 6 builds for Linux, Windows, macOS, and WebAssembly

The M68k CPU core starts from Musashi-derived source maintained directly in the CuteMac tree.

`HARDWARE_SUPPORT.md` maps the project's long-term scope — every pre-PCI Macintosh, its required and optional peripherals, and all pre-Mac OS X system software — against what CuteMac currently implements, with a traffic-light status for each entry.

## Build

Every third-party dependency, Qt included, comes from the [vcpkg](https://vcpkg.io) manifest in `vcpkg.json`, resolved through the vcpkg vendored at `third_party/vcpkg`. There are no system Qt or library packages to install.

```sh
git clone --recurse-submodules https://github.com/friedkiwi/CuteMac.git
cd CuteMac
./third_party/vcpkg/bootstrap-vcpkg.sh    # bootstrap-vcpkg.bat on Windows
```

Ubuntu/WSL2 still needs a host toolchain and the X11/audio development headers Qt builds against:

```sh
sudo apt-get install build-essential cmake ninja-build curl zip unzip tar pkg-config python3 bison
sudo apt-get install libgl1-mesa-dev libx11-dev libxkbcommon-x11-dev libxcb1-dev libfontconfig1-dev
sudo apt-get install libreadline-dev
```

The build also requires a Retro68 toolchain for the project-owned CuteMac Video declaration ROM. Put `m68k-apple-macos-as` and `m68k-apple-macos-objcopy` on `PATH`, install Retro68 at `/opt/Retro68/toolchain/bin`, or pass `-DCUTEMAC_RETRO68_TOOLCHAIN_BIN=/path/to/toolchain/bin` when configuring.

Configure through the presets so the toolchain file, triplet, and shared binary cache stay consistent:

```sh
cmake --preset debug          # or release, macos-release, linux-release
cmake --build --preset debug
ctest --preset debug
```

On Windows, run the presets from a Visual Studio developer command prompt so `cl.exe` and `ninja` are on `PATH`:

```
cmake --preset windows-msvc-debug
cmake --build --preset windows-msvc-debug
```

The presets point `VCPKG_BINARY_SOURCES` at the shared binary cache at `http://buildcache.cyber.gent/ac/{sha}`. With a warm cache, dependencies are downloaded rather than compiled; without it, the first configure builds Qt from source and takes a long time.

To build against a different cache, pass its URL when configuring:

```
cmake -S . -B build -DCUTEMAC_BINARY_CACHE_URL=http://cache.example.lan:8080/
```

The URL is a template: `{sha}` is appended if you leave it out. A `VCPKG_BINARY_SOURCES` set in the environment takes precedence over this flag.

The build produces `CuteMac`, the emulator application, plus `CuteMacDebugSession` for a readline debug console and headless Mac Plus/IIcx smoke tools.

The Windows Win64 workflow is temporarily manual while Retro68 installation is added to CI. Its `CuteMac-win64` artifact is a portable folder containing the release executables and their Qt 6 runtime dependencies.

`CuteMac` and `CuteMacDebugSession` share the machine-neutral `EmulationSession` facade. Desktop sessions run emulation off the Qt event thread; WebAssembly uses a single-threaded frame runner. Debug-only concrete machine access is kept behind an optional debug interface.

## Run

```sh
./build/CuteMac                      # session manager
./build/CuteMac "Mac Plus"           # start a profile by name
./build/CuteMac path/to/profile.toml # start a profile file directly
```

Run without arguments, `CuteMac` opens the session manager. Given a profile name or a path to a profile TOML file, it starts that machine directly in an emulator window. It stores TOML profiles in Qt's per-user app config location and uses Qt's per-user app data location for default ROM and disk-image folders.

Starting a profile from the session manager opens the emulator window in the same process, and the manager stays open so several machines can run side by side. One profile can only back one session at a time; starting a profile that is already running raises its existing window.

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

Serial ports can be configured from the profile dialog. The Hayes modem supports phonebook dialing, optional direct `host:port` TCP dialing, telnet negotiation filtering for phonebook TCP targets, and SLIP/PPP networking through libslirp when CuteMac is built with `libslirp-dev`. Phone number `1000` maps to the SLIP/libslirp backend by default, while `1001` maps to PPP/libslirp. Guest-side SLIP setup should use the configured guest IP as the Macintosh IP address, the configured host IP as router/gateway and DNS server, subnet mask `255.255.255.0`, and MTU `1006` unless changed in the profile. PPP negotiates the guest address and DNS through IPCP, and accepts blank authentication or any username/password:

```toml
[[serial.devices]]
channel = 0 # SCC A, modem port
type = "hayes_modem"
direct_tcp_dialing = false

[serial.devices.slip]
enabled = true
local_ip = "172.16.0.1"
remote_ip = "172.16.0.2"
mtu = 1006

[[serial.devices.phonebook]]
number = "1000"
target = "slip:libslirp"
telnet = false

[[serial.devices.phonebook]]
number = "1001"
target = "ppp:libslirp"
telnet = false
```

For debugger consoles, terminal tools, or null-modem workflows, configure a raw terminal/null-modem attachment. Listener mode exposes the emulated serial port on a TCP port; dial mode connects it to an existing TCP service. This endpoint does not parse modem AT commands:

```toml
[[serial.devices]]
channel = 0
type = "null_modem"
tcp_mode = "listen" # or "dial"
tcp_host = "127.0.0.1"
tcp_port = 2323
```

Mac Plus profiles can configure a 256-byte NVRAM image. RTC reads expose the current host-local date/time in the classic Macintosh epoch; guest time writes are intentionally discarded, while PRAM changes persist to this image:

```toml
[storage]
nvram_path = "/path/to/mac-plus.nvram"
```

New profiles default to unlimited speed. The shared machine configuration window provides capability-dependent general, floppy, SCSI, and NuBus tabs. Tools → ROM Manager scans the shared ROM folder by checksum and supplies machine and device ROMs centrally; profiles do not store ROM paths. The Tools menu also provides a disk image manager for typed floppy and hard-disk images.

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
