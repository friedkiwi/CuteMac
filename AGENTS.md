# CuteMac Agent Instructions

## Project Direction

- Build CuteMac as a modular classic Macintosh emulator using Qt 6 and CMake.
- Keep WebAssembly support in mind for all design choices; avoid platform-specific assumptions unless isolated behind small adapters.
- Primary development happens on WSL2, with intended targets of Windows, macOS on Intel/aarch64, Linux, and wasm.
- Target machines are 68000-era Macs, Macintosh IIcx, Macintosh Quadra 800, and Power Macintosh 6100.
- Support original, unmodified ROM files supplied by users. Do not embed copyrighted ROM content.
- Configuration files should use TOML syntax and should evolve toward a WinUAE/VMware Workstation style configuration workflow.

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
- Prefer clear interfaces between emulator core, devices, machine profiles, configuration, and Qt UI.
- Keep Qt UI code out of emulator core modules except for explicit frontend integration boundaries.
- Keep the GUI split as `CuteMac` for profile management and `CuteMacSession` for individual emulator windows unless there is a strong reason to change it.
- Keep debug-only interactive tools in `CuteMacDebugSession` so normal emulator sessions stay lean.
- `CuteMacDebugSession` owns bringup tooling: register/memory/disassembly commands, framebuffer export, Mac Plus sound-buffer capture export, bus logs, and the current minimal GDB remote stub.
- The GDB stub is intentionally minimal: it supports m68k register reads, memory read/write, single-step, continue, and software breakpoints. Full register writes and PC mutation are still future work.
- Use `QStandardPaths` for cross-platform profile, ROM, and disk-image defaults.
- Mac Plus bringup currently reaches the missing-floppy screen with the included user-supplied ROM profile. The startup sound can be exported from captured writes to the ROM sound buffer; live audio hardware emulation is still not complete.
- Mac Plus SCSI bringup can complete an NCR5380 READ(6) of the provided `work/system6withsw.dsk` image, but that image is raw HFS/Mini vMac-style media. The Plus ROM expects a SCSI disk to start with an Apple driver descriptor map (`ER`) and an Apple_Driver entry whose driver installs itself in the Unit Table. Reaching the System 6 desktop from this image requires synthesizing or generating a real Apple-compatible SCSI hard-disk wrapper and clean driver path.
- SCSI block targets should preserve Apple compatibility behavior, including ZuluSCSI-style Apple fixed-disk inquiry strings and mode page `0x30` with the Apple vendor string, so Apple Drive Setup and older Mac OS tools can identify the device.
- The annotated ROM reference may be cloned into ignored `work/mac_rom`; do not commit it.

## Build And Portability

- Use modern CMake targets and Qt 6 CMake integration.
- Avoid native-only APIs in shared code paths. Put host-specific code behind platform adapters.
- Keep wasm compatibility in mind when adding threading, filesystem, networking, timers, OpenGL/graphics, or JIT-related code.
- Verify changes with CMake builds when possible.
