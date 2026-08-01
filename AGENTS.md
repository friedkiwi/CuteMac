# CuteMac Agent Instructions

## Project Direction

- Build CuteMac as a modular classic Macintosh emulator using Qt 6 and CMake.
- Keep WebAssembly support in mind for all design choices; avoid platform-specific assumptions unless isolated behind small adapters.
- Primary development happens on WSL2, with intended targets of Windows, macOS on Intel/aarch64, Linux, and wasm.
- Target machines are 68000-era Macs, Macintosh IIcx, Macintosh Quadra 800, and Power Macintosh 6100.
- Support original, unmodified ROM files supplied by users. Do not embed copyrighted ROM content.
- Configuration files should use TOML syntax and should evolve toward a WinUAE/VMware Workstation style configuration workflow.
- Parse and serialize profiles with toml++; do not add ad hoc TOML parsing.

## Repository Practices

- Keep `README.md` concise and update it when setup or project direction materially changes.
- Maintain this `AGENTS.md` with persistent project instructions as decisions are made.
- Use the ignored `work/` directory for experiments, temporary docs, research artifacts, downloaded references, and throwaway notes.
- Do not commit contents of `work/`.
- Push git changes to `git@github.com:friedkiwi/CuteMac.git` after each completed change set when credentials/network allow it.

## Architecture Guidelines

- Separate CPU emulation cores from machine definitions and reusable hardware devices.
- The M68k CPU core starts from Musashi-derived source maintained directly in the CuteMac source tree, not as a third-party vendor subtree.
- Preserve provenance notices for imported or derived code under `licenses/`.
- Compose machine models from reusable devices where practical, for example SCSI, ADB, VIA, SCC, video, sound, memory, storage, and bus glue.
- Keep machine-specific behavior in machine profile or chipset modules rather than spreading conditionals through shared device code.
- ROM patches are optional profile features and default off. Apply them to an in-memory copy only, gate them by the original ROM SHA-256 and expected bytes, apply transactionally, and expose applied patch IDs in debug state. Never modify the user's ROM file.
- The Mac Plus v3 `skip_ram_pattern_test` patch skips only the destructive pattern passes. It preserves ROM checksum verification and RAM sizing, and updates the ROM's stored checksum to account for the branch edit.
- Prefer clear interfaces between emulator core, devices, machine profiles, configuration, and Qt UI.
- Frontends depend on `core::EmulationSession` and `core::IMachine`, never directly on a concrete machine. Concrete access is permitted only through `IDebugMachineAccess` in debug-only tooling.
- Timestamp host input with the machine cycle counter and deliver it through `MachineScheduler`; preserve button transitions long enough for guest VBL sampling.
- Desktop sessions run emulation through `SessionRunner` off the Qt event thread. WebAssembly uses the same runner API in single-threaded host-frame mode.
- Runtime speed is profile-controlled with `[runtime] speed = "realtime"|"unlimited"` and can change without resetting the guest. Desktop unlimited mode removes throttling; wasm unlimited mode uses a bounded host-frame work budget so the browser remains responsive.
- Unlimited desktop execution must leave a deterministic host-service window between emulation quanta. A scheduler yield alone does not prevent the worker from starving the session mutex and makes Qt input/framebuffer access unreliable.
- New profiles default to unlimited speed. The shared tabbed configuration UI derives IWM and SCSI tab visibility from machine device capabilities. Persist SCSI devices as typed targets with unique IDs 0-6; retain migration support for legacy `[storage]` paths.
- Keep managed floppy, CD-ROM, and hard-disk images in the shared disk-image catalog. Configuration and live insertion flows use the common media-type-filtered disk picker, and blank floppy/hard-disk creation shares the same image creation path.
- Keep host key mapping, capture policy, and framebuffer conversion in session frontend adapters. Wayland/wasm remain on the no-warp absolute-pointer path.
- Session management commands use newline-delimited JSON over a loopback-only control socket. Keep networking compiled out at runtime on wasm.
- Keep Qt UI code out of emulator core modules except for explicit frontend integration boundaries.
- Keep the GUI split as `CuteMac` for profile management and `CuteMacSession` for individual emulator windows unless there is a strong reason to change it.
- Keep debug-only interactive tools in `CuteMacDebugSession` so normal emulator sessions stay lean.
- Keep ROM-driver probes in `CuteMacDebugSession`; log Device Manager request fields at driver entry so failed media requests can be traced without adding normal-session hot-path overhead.
- `CuteMacDebugSession` owns bringup tooling: register/memory/disassembly commands, framebuffer export/probing, Mac Plus sound-buffer capture export, bus logs, trace rings, and the current minimal GDB remote stub. It uses libreadline for history and tab completion.
- Prefer the debug trace framework over ad hoc prints. Useful probes include `trace pc|irq|trap|driver|lowmem|iwm|floppy|timeline on`, `pc-trace`, `irq-trace`, `trap-trace`, `driver-trace`, `timeline`, `lowmem watch <name>`, `mem-find`, `mem-snapshot`, `memory-diff`, `bootblock verify`, `floppy last-window`, and `floppy export-window`.
- Debug execution helpers must advance devices and interrupts through the same timing path as normal execution. Do not step only the CPU in `run-until` or breakpoint handling.
- Until the machine has a cycle-callback/event scheduler, `MacPlusMachine::runCycles` must advance VIA timing after every instruction. Coarse CPU-only slices let ROM polling observe frozen devices and can strand `.Sony` timer waits.
- Keep high-volume capture off the normal session hot path. `MacPlusMachine` bus trace and sound capture default to disabled and should be enabled only by debug tooling or explicit tests.
- The GDB stub is intentionally minimal: it supports m68k register reads, memory read/write, single-step, continue, and software breakpoints. Full register writes and PC mutation are still future work.
- Use `QStandardPaths` for cross-platform profile, ROM, and disk-image defaults.
- Mac Plus bringup currently reaches the missing-floppy screen with the included user-supplied ROM profile. The startup sound can be exported from captured writes to the ROM sound buffer; live audio hardware emulation is still not complete.
- Mac Plus SCSI bringup can complete an NCR5380 READ(6) of the provided `work/system6withsw.dsk` image, but that image is raw HFS/Mini vMac-style media. The Plus ROM expects a SCSI disk to start with an Apple driver descriptor map (`ER`) and an Apple_Driver entry whose driver installs itself in the Unit Table. Reaching the System 6 desktop from this image requires synthesizing or generating a real Apple-compatible SCSI hard-disk wrapper and clean driver path.
- SCSI block targets should preserve Apple compatibility behavior, including ZuluSCSI-style Apple fixed-disk inquiry strings and mode page `0x30` with the Apple vendor string, so Apple Drive Setup and older Mac OS tools can identify the device.
- Stock Apple HD SC Setup additionally whitelists standard INQUIRY identities. Fixed disks advertise the known-compatible ` SEAGATE` / `          ST225N` identity and accept FORMAT UNIT; preserve exact field padding and regression coverage.
- Apple HD SC preparation uses MODE SELECT and FORMAT UNIT DATA OUT. FORMAT UNIT with `FMTDATA` first consumes its four-byte header, then the declared defect list and optional initialization pattern. Complete NCR5380 DATA OUT only on the ACK falling edge, and persist successful block writes to the backing image.
- Apple HD SC's post-format verification also requires VERIFY(10), READ DEFECT DATA with a zero-defect response, and MODE SENSE format/rigid-geometry pages. Derive reported cylinders from image capacity and keep these commands covered by target tests.
- `CuteMacSession` retains the source profile path and saves accepted configuration, speed, and removable-media changes. Prompt before applying configuration changes that rebuild/reset the emulated machine; name and speed changes do not require reset.
- Mac Plus IWM/floppy bringup has a reusable raw 400K/800K and Disk Copy 4.2 image loader that feeds nibblized GCR track data through the IWM data register. Apple System 6.0.8 800K Disk Copy images can be downloaded/extracted under ignored `work/bringup/system608/` for testing. `CuteMacDebugSession` can inspect the generated stream with `floppy scan [track] [side]` and `floppy export-track <file> [track] [side]`.
- Keep IWM controller-enable state separate from Sony drive mechanism state. The IWM soft switch controls status bit 5; MOTORON/MOTOROFF drive-register strobes latch the mechanism motor state. Preserve ROM-visible drive sense polarity, notably WRTPRT=1 for writable media and SIDES=1 for a double-sided drive.
- Add focused CTest coverage for device register protocols as they become executable. IWM tests should reproduce the unmodified ROM's soft-switch ordering rather than calling private implementation helpers.
- The previous `.Sony` motor/seek wait at PC `0x402424` was caused by the VIA IFR read path fabricating Timer 1 on every read, starving the ROM's Timer 2 callback. VIA Timer 1/2 flags now come from timer state instead of IFR reads, and the ROM gets past that wait.
- VIA input pins must remain separate from output latches and be combined according to DDR on reads. In particular, PB3 is the active-low mouse button input; losing it makes the ROM treat the mouse as held and deliberately eject boot media.
- VIA IFR bit 1 is the Mac Plus CA1 vertical-blank source, not 6522 Timer 1 (bit 6). Generate CA1 at 60.15 Hz so ROM VBL cursor, button, keyboard, and task-queue processing runs.
- Low-memory `Ticks` advances only through the ROM's CA1 VBL handler. Never synthesize time on reads; doing so breaks debounce, double-click timing, delays, and event timestamps.
- Preserve 6522 timer acknowledgement semantics: reading T1C-L clears IFR6 and reading T2C-L clears IFR5. Timer-driven guest services otherwise leave IRQ asserted and can trap Finder in an interrupt storm.
- Mac Plus RTC/PRAM traffic is bit-banged through VIA PB0 (data), PB1 (clock), and active-low PB2 (enable). Supply a valid Macintosh-epoch clock and PRAM signature; returning a floating-high `0xffffffff` date causes Alarm Clock to block Finder while normalizing the invalid date.
- Mac Plus host input must enter through ROM-visible hardware semantics. Mouse positions feed the SCC-produced `MTemp` accumulator without pre-updating `RawMouse`, button state changes only PB3 so VBL can post transitions, and keyboard transitions are returned through VIA SR inquiry responses rather than written into `KeyMap`.
- Deliver host keyboard input as state edges: suppress duplicate key-down/key-up events and release tracked guest keys when the display loses focus. Retain a Mac Plus mouse-button edge for one VBL so the ROM observes it, but do not stretch ordinary clicks across several VBLs because classic controls interpret that as auto-repeat.
- `CuteMacSession` captures relative mouse input when the emulated display is clicked. Release uses Ctrl+Alt on Windows/Linux and Control+Option on macOS, and the host-specific chord must remain visible in the View menu.
- Mouse capture must have a no-warp fallback for Wayland and wasm. In fallback mode, map widget coordinates directly to guest coordinates and never call `grabMouse()` or `QCursor::setPos()`; these operations are unavailable or restricted on those platforms.
- Release guest mouse capture before opening modal session dialogs. Keep configuration device tables out of inline edit mode; Wayland input-method focus recursion can otherwise overflow the stack when a captured session opens the table.
- Current floppy boot blocker: System 6.0.8 Disk 1 generates MAME/ROM-compatible GCR tracks: DC42/raw block order is cylinder/head/sector, sector slot order uses the ROM/MAME interleave, and exported track 0 side 0 round-trips tags/data/checksums with a MAME-style decoder. A 100,000,000-cycle debug run is still in the ROM's early memory-test area around PC `0x40035c`, before `.Sony` performs media I/O; use a later checkpoint or resolve CPU/memory-test throughput before drawing conclusions from an empty IWM nibble trace.
- The annotated ROM reference may be cloned into ignored `work/mac_rom`; do not commit it.

## Build And Portability

- Use modern CMake targets and Qt 6 CMake integration.
- Avoid native-only APIs in shared code paths. Put host-specific code behind platform adapters.
- Keep wasm compatibility in mind when adding threading, filesystem, networking, timers, OpenGL/graphics, or JIT-related code.
- Verify changes with CMake builds when possible.
