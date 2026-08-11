# CuteMac Agent Instructions

Read this file in full before starting work. Device-level bringup findings live
in `docs/hardware-notes.md`; read the relevant section there before changing
video/NuBus, SCSI, floppy, VIA/ADB/audio, or machine-specific chipset code.

## Working Practices

- **Push to `git@github.com:friedkiwi/CuteMac.git` after each completed change set**, whenever credentials and network allow. An unpushed commit does not exist for CI: a workflow run will build the previous commit and fail for a bug already fixed locally.
- Verify changes with CMake builds when possible, and add focused CTest coverage for device register protocols as they become executable. Tests should drive unmodified-ROM orderings rather than private implementation helpers.
- Keep this file current as decisions are made, and short enough to read in one pass — new device-level detail belongs in `docs/hardware-notes.md`.
- Keep `README.md` concise; update it when setup or project direction materially changes.
- Use the ignored `work/` directory for experiments, temporary docs, research artifacts, downloaded references, and throwaway notes. Never commit its contents, including the annotated ROM reference cloned to `work/mac_rom`.

## Project Direction

- Build CuteMac as a modular classic Macintosh emulator using Qt 6 and CMake.
- Keep WebAssembly support in mind for all design choices; avoid platform-specific assumptions unless isolated behind small adapters.
- Primary development happens on WSL2, targeting Windows, macOS on Intel/aarch64, Linux, and wasm.
- Target machines are 68000-era Macs, Macintosh IIcx, Macintosh Quadra 800, and Power Macintosh 8100/80.
- Support original, unmodified ROM files supplied by users. Do not embed copyrighted ROM content.
- ROM patches are optional profile features and default off. Apply them to an in-memory copy only, gate them by the original ROM SHA-256 and expected bytes, apply transactionally, and expose applied patch IDs in debug state. Never modify the user's ROM file.
- Configuration files use TOML, parsed and serialized with toml++ — no ad hoc TOML parsing. Evolve toward a WinUAE/VMware Workstation style configuration workflow.

### Power Macintosh 8100/80

- The target is the launch-model 8100/80. It shares the 4 MiB `9FEB69B3` ROM with the 6100/60 and 7100/66, but must model the 8100's own memory topology, three native NuBus slots, BART controller configuration, PDS/video configuration, interrupts, and 80 MHz PowerPC 601 with a 40 MHz bus.
- Implement the PowerPC 601 core as a single portable interpreter. Do not add a backend abstraction, JIT interface, optimized-backend hooks, or speculative dual-engine design; improve the interpreter itself in response to profiling, and revisit the architecture only if measured performance requires it.
- Integrate `PowerPc601Core` through the same machine-neutral boundaries as the 68k core: derive it from `core::CpuCore`, expose reset, interrupt, instruction-step, cycle-budget execution, program-counter, register snapshot, and disassembly operations, and keep machine/chipset and Qt dependencies out of the CPU. Its `PowerPcBus` performs big-endian physical 8/16/32-bit accesses analogous to `M68kBus`; effective-address translation, protection, precise exceptions, and 601 architectural state remain inside the CPU.
- The machine owns cycle scheduling, the physical address map, BART/bus glue, and interrupt routing. Advance the 601 and devices through `MachineScheduler` using instruction cycle counts, with no host clocks or timers in the CPU. Reuse existing device layers only at valid boundaries: NuBus bus/cards and the framebuffer contract directly; SCSI targets/media, ADB endpoints, floppy media, PRAM persistence/date overlay, and other protocol-independent state beneath 8100-specific controllers or adapters. Do not reuse a controller model when the 8100 hardware protocol differs.
- Treat PowerPC tracing and `CuteMacDebugSession` support as part of initial 601 development, not post-bringup polish. Extend the debug boundary with architecture-tagged CPU state and disassembly, and add bounded, opt-in rings for instructions, exceptions, interrupts, effective-to-physical translations, and physical bus accesses. Every record carries the machine cycle and relevant PC; exception records include vector/cause and saved architectural state, while translation records include access type, address, result, and protection/fault outcome. Keep capture disabled at zero/near-zero normal-session cost, make trace save output deterministic for differential comparison, and ensure step, run-until, breakpoints, and tracing all advance the complete machine timing path.

## Architecture

### Boundaries

- Separate CPU emulation cores from machine definitions and reusable hardware devices, and prefer clear interfaces between emulator core, devices, machine profiles, configuration, and Qt UI.
- Compose machine models from reusable devices where practical: SCSI, ADB, VIA, SCC, video, sound, memory, storage, and bus glue. Keep machine-specific behavior in machine profile or chipset modules rather than spreading conditionals through shared device code.
- Frontends depend on `core::EmulationSession` and `core::IMachine`, never directly on a concrete machine. Concrete access is permitted only through `IDebugMachineAccess` in debug-only tooling.
- Keep Qt UI code out of emulator core modules except at explicit frontend integration boundaries.
- Attach serial peripherals through the controller-independent `SerialEndpoint`/`SerialBus` boundary. The ImageWriter II is a serial device, while sequential PNG encoding is an interchangeable host output sink; do not couple SCC implementations to printers or filesystem output.
- Keep LocalTalk line/protocol emulation behind a controller-independent endpoint boundary. The default may be an unattached idle-wire stub, while future host bridges and virtual networks are selectable devices in the configuration UI like serial printers; do not couple those wrappers to the Z8530 SCC model.
- Keep optional guest integrations behind versioned device protocols. CuteMac Video exposes a declaration-ROM `.CuteMac` service and a synchronous MMIO mailbox.
- The M68k CPU core starts from Musashi-derived source maintained directly in the CuteMac source tree, not as a third-party vendor subtree. Preserve provenance notices for imported or derived code under `licenses/`.
- No guest instruction may end the host process. The FPU's imported `fatalerror()` used to call `exit(1)`, so a single unimplemented encoding killed CuteMac with one line on stderr; it now reports through `cutemac_m68k_fpu_report` and raises line-1111, which is what the hardware does. Keep it that way in any imported CPU code: an encoding CuteMac cannot execute is a guest exception plus a diagnostic record, never a host exit.
- Floating-point arithmetic is shared across the 68881, 68882 and 68040; the `FSAVE`/`FRESTORE` state frame format is not, because guest software reads the frame's format byte to identify the coprocessor. Machines select their part with `M68kCpuCore::setFpuModel()` after `setModel()`, which resets it to the part that shipped with the CPU.
- Transcendental FPU operations go through host libm on a converted `double`, giving 53 mantissa bits against the hardware's 64. This is a deliberate, documented limitation inherited from the existing `FSIN`/`FCOS`; exact operations such as `FSCALE` and `FGETMAN` stay in `floatx80`. Do not use `long double` to close the gap: it is 64 bits on MSVC and arm64 macOS, so accuracy would vary by host.
- Gate address-translation behavior by the selected M68k CPU: 68000/68010 and EC parts have no PMMU, a full 68020 may explicitly attach an external MC68851, the 68030 uses its integrated PMMU, and the 68040 has a distinct native-MMU dispatch boundary rather than reusing 68030 descriptors or instructions. Keep debugger translation side-effect free and report physical bus faults separately from MMU translation faults.
- Video devices own emulated VRAM, mode, CLUT, and scanout interpretation, and publish frontend-neutral `VideoFrame` snapshots. Indexed frames specify packed depth and pixel-to-CLUT mapping; direct-color frames specify depth, byte order, and channel masks. Qt owns the resulting `QImage`; never put frontend bitmap types in emulated devices.

### Execution And Timing

- Desktop sessions run emulation through `SessionRunner` off the Qt event thread. WebAssembly uses the same runner API in single-threaded host-frame mode.
- Runtime speed is profile-controlled with `[runtime] speed = "realtime"|"unlimited"` and can change without resetting the guest. Desktop unlimited mode removes throttling; wasm unlimited mode uses a bounded host-frame work budget so the browser remains responsive. New profiles default to unlimited.
- Unlimited desktop execution must leave a deterministic host-service window between emulation quanta. A scheduler yield alone does not prevent the worker from starving the session mutex and makes Qt input/framebuffer access unreliable.
- While interactive host input is held, temporarily pace unlimited execution at realtime so a normal wall-clock click or keypress does not span thousands of guest VBLs and trigger Finder auto-repeat. Queue the release before removing this temporary throttle, and retain realtime pacing through the host double-click interval so the guest's `DoubleTime` window does not expire between clicks; do not change the profile's configured speed.
- Timestamp host input with the machine cycle counter and deliver it through `MachineScheduler`; preserve button transitions long enough for guest VBL sampling.

### Frontend And Session

- Ship the GUI as the single `CuteMac` binary. Started without arguments it opens `ProfileManagerWindow`; started with a profile name or profile file path it resolves that request through `config::resolveProfile` and opens the machine directly. Keep `main.cpp` limited to mode selection. A separate session executable would reintroduce two macOS app bundles and put the session manager out of reach on wasm.
- `session::SessionWindowManager` owns every live `EmulatorWindow` and keys them by the canonical profile path, so one profile can never be driven by two windows writing the same file. Route all session creation through it, and have the profile manager refresh and gate destructive actions on its sessions-changed callback. This project builds without moc; use plain callbacks rather than adding `Q_OBJECT` and Qt signals.
- Profile lookup by name scans the managed profile directory instead of probing `profilePathForName`. Case-insensitive filesystems answer an `exists()` probe for a path cased differently than the file on disk, and the returned path becomes session identity.
- `EmulatorWindow` retains the source profile path and saves accepted configuration, speed, and removable-media changes. Prompt before applying configuration changes that rebuild/reset the emulated machine; name and speed changes do not require reset.
- `EmulatorWindow` clicks into relative mouse capture (`grabMouse()` + recentering `QCursor::setPos()`) only when the currently displayed `VideoFrame` is marked grabbable. ADB-less compact Mac outputs and CuteMac Video/Video Accelerated outputs with the integrated absolute pointer enabled are not grabbable; authentic cards and CuteMac Video with absolute pointer disabled preserve relative ADB movement. Release with Ctrl+Alt on Windows/Linux, Control+Option on macOS, always visible in the Display menu and in the title bar while captured; do not draw release instructions over the emulated framebuffer.
- Never assume a requested grab succeeded. `Q_OS_WASM` and Wayland platform names skip the attempt outright; elsewhere, check `mouseGrabber() == this` after calling `grabMouse()` and fall back to absolute widget-to-guest coordinate mapping (no warp, no grab) if it didn't take. A declined or unsupported grab must never leave the pointer in a half-captured, erratic state.
- While relative capture is active, keep the host cursor hidden and recentered from the session frontend. Ignore synthetic warp events so recentering cannot create guest deltas, and release capture if the platform drops the grab or refuses recentering.
- Leaving the emulated display, losing focus, or opening a modal session dialog must release any active capture, restore the host cursor, and release any held guest mouse button.
- Deliver host keyboard input as state edges: suppress duplicate key-down/key-up events and release tracked guest keys when the display loses focus.
- Keep host key mapping, capture policy, and framebuffer conversion in session frontend adapters. Keep configuration device tables out of inline edit mode; Wayland input-method focus recursion can otherwise overflow the stack.
- Emulator windows provide Display → Zoom choices for 1x, 1.5x, 2x, and Custom. At startup choose the largest of 2x or 1.5x that fits comfortably within the active screen's available geometry, otherwise use 1x. Preset selection requests the matching window size; manual resize selects Custom. Preserve aspect ratio and center with letterboxing when the host cannot honor the requested size.
- Session management commands use newline-delimited JSON over a loopback-only control socket. Keep networking compiled out at runtime on wasm.

### Configuration And Media

- Use `QStandardPaths` for cross-platform profile, ROM, and disk-image defaults.
- Keep user-supplied machine and device ROM discovery centralized in `rom::RomCatalog`. Match files by checksum, keep ROM paths out of profiles, and list project-owned embedded ROMs in the same catalog.
- The shared tabbed configuration UI derives floppy, SCSI, and NuBus tab visibility from machine device capabilities. Persist SCSI targets with unique IDs 0-6 and NuBus cards with unique machine-valid slots; retain migration support for legacy `[storage]` paths.
- Machine RAM sizes are catalog-defined discrete configurations, stored in KiB so fractional-MiB hardware configurations remain representable. The GUI, TOML loader/saver, and machine factory must use the same catalog validation; retain migration support for legacy `ram_size_mib` profiles.
- Keep managed floppy, CD-ROM, and hard-disk images in the shared disk-image catalog. Configuration and live insertion flows use the common media-type-filtered disk picker, and blank floppy/hard-disk creation shares the same image creation path.
- Represent disk-image collections as nested directories inside the managed image library. Keep image paths as catalog identity, discover nested images recursively, and present the same searchable collection tree in manager and picker workflows.
- Derive disk-image volume identifiers with small bounded reads rather than loading full media: recognize classic HFS/MFS, Disk Copy 4.2 wrappers, Apple Partition Map HFS volumes, and ISO-9660 when possible, and leave unknown formats blank.

### Debug Tooling

- Keep debug-only interactive tools in `CuteMacDebugSession` so normal emulator sessions stay lean. It owns bringup tooling: register/memory/disassembly commands, framebuffer export/probing, Mac Plus sound-buffer capture export, bus logs, trace rings, and the current minimal GDB remote stub. It uses libreadline for history and tab completion.
- Keep common CPU execution, register, memory, disassembly, framebuffer, and machine-state commands available for every machine through the debug access boundary. Machine-specific probes may remain explicitly gated; IIcx bringup also exposes NuBus slot and I/O summaries.
- Keep ROM-driver probes in `CuteMacDebugSession`; log Device Manager request fields at driver entry so failed media requests can be traced without adding normal-session hot-path overhead.
- Prefer the debug trace framework over ad hoc prints. Useful probes include `trace pc|irq|trap|driver|lowmem|iwm|floppy|timeline on`, `pc-trace`, `irq-trace`, `trap-trace`, `driver-trace`, `timeline`, `lowmem watch <name>`, `mem-find`, `mem-snapshot`, `memory-diff`, `bootblock verify`, `floppy last-window`, and `floppy export-window`.
- Debug execution helpers must advance devices and interrupts through the same timing path as normal execution. Do not step only the CPU in `run-until` or breakpoint handling.
- Keep high-volume capture off the normal session hot path; it defaults to disabled and is enabled only by debug tooling or explicit tests.
- The GDB stub is intentionally minimal: it supports m68k register reads, memory read/write, single-step, continue, and software breakpoints. Full register writes and PC mutation are still future work.
- Panic dumps capture whole-machine state to a zip archive for offline analysis. `CUTEMAC_ENABLE_PANIC_DUMP` is on by default but resolves per configuration through generator expressions, so only Debug builds carry the feature and libzip; wasm forces it off. Triggers are the toolbar `Panic!` button, `Machine -> Panic Dump...` (Ctrl/Cmd+Shift+P), the control-socket `panic` command, and `panic write` in `CuteMacDebugSession`. Archives land in `debug_dumps` under `QStandardPaths::AppDataLocation`, overridable with `CUTEMAC_PANIC_DUMP_DIR`. Every trigger announces itself and the written path on stdout; Debug Windows builds therefore keep a console (`WIN32_EXECUTABLE` is off for Debug).
- Machines contribute panic state through `IMachine::debugSnapshot()`, which returns the frontend-neutral `debug::MachineSnapshot`. Convert existing per-device `debugState()` structs with the helpers in `cutemac/debug/SnapshotBuilder.h` rather than formatting state at the call site, and give new NuBus cards a `NuBusCard::debugSnapshot()` override so a dump can say what is in the slot. Capture takes the session lock with a bounded wait and proceeds anyway on timeout, recording a `degraded:` note: a panic button that deadlocks on the hang being investigated is worse than none.
- Panic archives load back into `CuteMacDebugSession` with `panic open <archive>` or `--panic <archive>`, which builds a read-only `debug::SnapshotMachine` behind `IDebugCpuAccess`. Register, memory, disassembly, screen, device, and low-memory commands work unchanged; execution, writes, and media commands are refused. Opening a snapshot tears down the live machine first because Musashi resolves disassembly through one file-scope active-CPU pointer.
- The console's `pc`/`irq`/`trap`/`driver`/`timeline` rings are still filled by the debug session's own stepping loop, not by the machines, so they only reach a dump written with `panic write`. Moving them into the emulator core means making the machines emit the records on their execution path; the archive schema already reserves `trace/*.txt` for that.

## Build And Portability

- Use modern CMake targets and Qt 6 CMake integration.
- All third-party dependencies, Qt included, are declared in `vcpkg.json` and resolved through the vcpkg vendored at `third_party/vcpkg`. Do not vendor individual libraries as submodules, do not add `FetchContent`, and do not fall back to system packages or an external Qt installer kit. Silent fallbacks are what hid the missing Windows libslirp.
- Use the shared vcpkg HTTP binary cache at `http://buildcache.cyber.gent/ac/{sha}` for local and CI builds. `CMakePresets.json` sets `VCPKG_BINARY_SOURCES` to `clear;http,http://buildcache.cyber.gent/ac/{sha},readwrite`; CI sets the same value in the workflow environment. Preserve this unless cache infrastructure changes by explicit decision — without it every clean build compiles Qt from source.
- Configure through `CMakePresets.json` rather than ad hoc flags, so the toolchain file, triplet, and binary cache stay consistent. Windows presets expect a Visual Studio developer command prompt; `windows-msvc-ci` uses the multi-config generator instead.
- A bare `cmake ..` reads no presets, so `cmake/VcpkgBootstrap.cmake` runs before `project()` and supplies the same setup: it bootstraps `third_party/vcpkg` if the tool is missing, defaults `VCPKG_BINARY_SOURCES` to the shared cache when the environment has not set it, and points `CMAKE_TOOLCHAIN_FILE` at the vendored toolchain. `-DCUTEMAC_BINARY_CACHE_URL=<url>` retargets that default at a different cache without spelling out the `VCPKG_BINARY_SOURCES` grammar; `{sha}` is appended when the URL omits it, and an environment variable still wins over the flag. Keep it ahead of `project()` — the toolchain must be in place for the first compiler check, which is when vcpkg installs the manifest.
- Comment fields inside `vcpkg.json` dependency objects must be spelled `$comment`. vcpkg ignores `$`-prefixed keys but rejects the manifest outright on an unknown plain `comment` key.
- Optional dependencies must report their state at configure time and fail loudly when a declared manifest dependency is missing. Never let a shipped feature degrade silently: `CUTEMAC_ENABLE_SLIRP` is on by default, forced off only for wasm, and its `pkg_check_modules` call is `REQUIRED`.
- libslirp exposes only pkg-config metadata and the vcpkg toolchain does not provide pkg-config, so the manifest supplies `pkgconf` as a host dependency and CMake points `PKG_CONFIG_EXECUTABLE` at it before `find_package(PkgConfig)`.
- Windows and macOS artifacts must be self-contained: `windeployqt` populates the Windows build tree and `macdeployqt` bundles Qt plus the libslirp/glib dylibs into `CuteMac.app`. Never let a release artifact reference a developer machine's Homebrew or Qt installation. Linux packages declare dependencies instead and get no deployment step.
- Do not pass `--release` or `--debug` to `windeployqt`; let it classify the binary. Explicit flags make it fail to locate the platform plugin on some Qt kits.
- Keep the distributable product explicit: development utilities, smoke tools, and tests have no install rules, so CPack packaging cannot pick them up by scanning build output.
- Avoid native-only APIs in shared code paths; put host-specific code behind platform adapters. Keep wasm compatibility in mind when adding threading, filesystem, networking, timers, OpenGL/graphics, or JIT-related code.
