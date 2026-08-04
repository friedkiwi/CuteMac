# CuteMac Hardware Notes

Device-level findings from bringup, split out of `AGENTS.md` so that file stays
short enough to read in one pass. These are standing constraints, not history:
each one records behaviour some unmodified ROM or system version depends on, and
most were found by debugging a boot failure. Read the relevant section before
changing code in that subsystem.

## Video And NuBus

- Macintosh IIcx video bringup starts with a reusable NuBus bus and the authentic Apple Macintosh II Video Card (630-0153): external declaration ROM, 512 KiB VRAM, Bt453-style palette, 640x480 output, 1/2/4/8-bit modes, and slot interrupts. Generalize the frontend framebuffer contract before implementing it; do not retain Mac Plus-specific dimensions or monochrome assumptions.
- Map the Macintosh II Video Card's `342-0008-A` declaration ROM like the physical lane-0 device: reverse the 4 KiB dump, account for its inverted data and `0xE1` byte-lane descriptor, expand it to the 16 KiB sparse NuBus view, and leave unused lanes high. A dense raw-ROM mapping is not Slot Manager compatible.
- The 630-0153 VBL control handler uses byte-addressed offsets: `+0` acknowledges/enables and `+4` disables. Do not scale these to longword offsets when crossing the generic NuBus API.
- NuBus register devices apply read side effects only on physically connected byte lanes. Undriven Bt453 lanes return open-bus data without advancing palette state.
- Also offer an explicitly non-historical `CuteMac Video` NuBus card with a project-owned declaration ROM/68k driver, configurable dimensions and color depth, coherent guest-readable VRAM, dirty-region reporting, and a versioned bounded command interface for optional host-accelerated fills, copies, monochrome expansion, palette updates, and cursor operations. Keep a software backend, validate every guest offset/stride/rectangle, and keep Qt/GPU APIs off the emulation thread and out of the device model.
- Develop acceleration in the separate `CuteMac Video Accelerated` adapter, initially delegating the known-working `CuteMac Video` behavior. Do not alter the original adapter or its driver for acceleration experiments; promotion behind the existing acceleration checkbox is a later explicit decision.
- Reuse the NuBus framework and NuBus video cards in the Quadra 800, including support for its compatible faster NuBus implementation. Model the Quadra 800's onboard DAFB-family video separately.
- The CuteMac NuBus video declaration-ROM driver implements classic `SetMode`, `SetEntries`, and `GetEntries`; indexed color must flow through the card's guest-programmable RAMDAC registers rather than a frontend-only palette.
- Build the CuteMac Video declaration-ROM driver from `src/devices/video/nubus/guest/cutemac_video_driver.S` with Retro68's `m68k-apple-macos-as` and `m68k-apple-macos-objcopy` through CMake. Keep the assembly as the sole driver-bytecode source of truth; never hand-maintain a C++ byte array.
- Video `Control` and `Status` calls store a pointer to the selector-specific record in `csParam`; never treat the parameter record as inline in the Device Manager request. System 6 passes the `gdDevType` Boolean at `SetGray`/`GetGray` `csMode` offset zero (zero monochrome, one colour), which the driver translates to its inverse luminance-mapping flag. Do not substitute the different Mini vMac virtual-video convention: doing so activates grayscale during System 6 startup and causes a reset loop with a saved 256-level setting. Preserve logical CLUT entry zero as white and the last entry for each indexed depth as black.
- Before calling `JIODone`, the CuteMac NuBus video driver must replace `ioInProgress` in the queued request's `ioResult` with its actual result. Otherwise a queued `SetGray` blocks the following `SetMode`, leaving QuickDraw and hardware scanout at different depths.
- CuteMac Video mode records publish version-1 video PixMap parameters so Color QuickDraw adopts each depth's row layout. System 6 may load a new mode's CLUT before `SetMode`; treat `ColorSpec.value` and sequential indices as mode-independent physical RAMDAC indices and mask flag bits above bit 7. Video Manager passes `csCount` as a zero-based record count, so use it directly as a `DBRA` terminal value. Program the caller-provided RGB values because `SetGray` may follow `SetEntries`, while preserving physical RAMDAC entries 0 and 255 as white and black endpoints.
- CuteMac Video absolute-pointer integration is profile-configurable and defaults on. Publish host coordinates through the versioned guest-services mailbox and synchronize `MTemp`, `RawMouse`, `Mouse`, and `CrsrNew` in the slot-VBL driver before `JVBLTask`; do not route integrated movement through ADB. When disabled, and for authentic video cards, preserve relative ADB mouse semantics.
- Never call the System 6 Shutdown Manager from the CuteMac Video driver's early `Open` path; install its ROM-resident shutdown callback only through the explicit guest-helper control after system traps are initialized.
- System 6 starts the IIcx in 24-bit addressing mode. Translate logical standard-slot windows `$s00000-$sfffff` to NuBus `$fs000000-$fs0fffff`, keep the CuteMac Video control aperture inside that one-megabyte window, and have its declaration-ROM driver use the logical window. Do not advertise `f32BitMode` until 68030 PMMU/address-mode translation is implemented and verified.
- The IIcx color cursor advances from the video card's slot VBL, not VIA1 VBL alone. A video declaration-ROM driver must install its slot interrupt with `SIntInstall`, acknowledge the card, call `JVBLTask`, and remove the interrupt on close; preserve the DCE pointer across Slot Manager traps.
- The Apple NuBus Ethernet Card uses the `aenet1` declaration ROM, byte lanes 0 and 2 (`0xa5`), local packet RAM at `$FssD0000-$FssDFFFF`, and DP8390 registers at `$FssE0000-$FssE003F`. Keep the DP8390/card model independent of host networking and attach host connectivity only through the packet-level `PacketNetworkBackend`.

## SCSI

- Mac Plus SCSI bringup can complete an NCR5380 READ(6) of the provided `work/system6withsw.dsk` image, but that image is raw HFS/Mini vMac-style media. The Plus ROM expects a SCSI disk to start with an Apple driver descriptor map (`ER`) and an Apple_Driver entry whose driver installs itself in the Unit Table. Reaching the System 6 desktop from this image requires synthesizing or generating a real Apple-compatible SCSI hard-disk wrapper and clean driver path.
- SCSI block targets should preserve Apple compatibility behavior, including ZuluSCSI-style Apple fixed-disk inquiry strings and mode page `0x30` with the Apple vendor string, so Apple Drive Setup and older Mac OS tools can identify the device.
- Keep NCR5380 target handshakes separate from Macintosh pseudo-DMA bus glue. A DACK without asserted REQ does not transfer a byte. The IIcx GLUE aperture must nevertheless support the SCSI Manager's blind consecutive accesses by observing/waiting for DRQ before completing each CPU access; do not require the guest to insert a Bus-and-Status read. MAME models this boundary with a Macintosh-specific four-byte FIFO, CPU wait states, and timeout/bus-error handling; CuteMac's synchronous adapter may be simpler but must preserve both checked and blind transfer semantics.
- Route Macintosh NCR5380 accesses through the reusable `MacintoshNcr5380Bus` transaction adapter. Configure register/DACK byte lanes and blind-transfer burst width in the machine chipset; never decompose a side-effectful CPU word access into independent byte accesses. The Mac Plus uses 68000 low-byte smearing with one NCR5380 strobe per word, while Mac II-family blind word/longword apertures may produce ordered multi-byte bursts.
- NCR5380 pseudo-DMA sends retain the final DATA OUT byte/phase while the last byte drains through the ACK handshake. A DACK write loads that byte but does not synchronously advance to STATUS; the IIcx ROM must first observe the still-matching phase, while a loaded disk driver must subsequently be able to observe the phase mismatch even before clearing DMA mode.
- SCSI CD-ROM targets use 2048-byte ISO sectors and ZuluSCSI's Apple-compatible `MATSHITA` / `CD-ROM CR-8004` identity. Keep them read-only and removable, with SCSI-2 READ, READ CAPACITY, MODE SENSE, START STOP, and READ TOC support.
- Configured CD-ROM drives remain selectable when empty and report unit-attention/media-changed sense after runtime insertion or ejection. The session toolbar owns insert/change/eject workflows for configured SCSI media without resetting the guest.
- Stock Apple HD SC Setup additionally whitelists standard INQUIRY identities. The ` SEAGATE` / `          ST225N` compatibility identity represents only the legacy Apple 20SC class and must not be the generic fixed-disk identity; fixed disks accept FORMAT UNIT and preserve exact INQUIRY field padding.
- Do not advertise the 20 MB ST225N identity for arbitrary hard-disk images. Derive the fixed-disk INQUIRY personality from the catalog size (Conner CP2025 at 20 MiB; Quantum GO40S/GO80S1/GO160S, LP240S, and LPS540S at the matching presets; IBM DPES-31080 at 1 GiB), and use padded `QUANTUM` / `FIREBALL1` fields for custom sizes. Keep the image-backed READ CAPACITY and MODE geometry authoritative.
- Apple HD SC preparation uses MODE SELECT and FORMAT UNIT DATA OUT. FORMAT UNIT with `FMTDATA` first consumes its four-byte header, then the declared defect list and optional initialization pattern. Complete NCR5380 DATA OUT only on the ACK falling edge, and persist successful block writes to the backing image.
- Apple HD SC's post-format verification also requires VERIFY(10), READ DEFECT DATA with a zero-defect response, and MODE SENSE format/rigid-geometry pages. Derive reported cylinders from image capacity and keep these commands covered by target tests.

## Floppy: IWM And SWIM

- Mac Plus IWM/floppy bringup has a reusable raw 400K/800K and Disk Copy 4.2 image loader that feeds nibblized GCR track data through the IWM data register. Apple System 6.0.8 800K Disk Copy images can be downloaded/extracted under ignored `work/bringup/system608/` for testing. `CuteMacDebugSession` can inspect the generated stream with `floppy scan [track] [side]` and `floppy export-track <file> [track] [side]`.
- Keep IWM controller-enable state separate from Sony drive mechanism state. The IWM soft switch controls status bit 5; MOTORON/MOTOROFF drive-register strobes latch the mechanism motor state. Preserve ROM-visible drive sense polarity, notably WRTPRT=1 for writable media and SIDES=1 for a double-sided drive.
- IWM tests should reproduce the unmodified ROM's soft-switch ordering rather than calling private implementation helpers.
- IIcx SWIM1 enters ISM mode through the IWM write sequence `0x40,0x00,0x40,0x40`. Raw 800K System 6 GCR media reuses the common floppy image/track encoder through ISM data, phase, mode, setup, and handshake registers.
- IIcx SuperDrive support accepts raw and Disk Copy 4.2 1.44 MB images and exposes 80-track, two-sided, 18-sector MFM media through SWIM1 ISM mark-aware reads, CRC fields, active-low high-density and ready sensing, and the native rising-edge drive-command strobe. Preserve the separate IWM/GCR drive-command path used by 400K/800K media.

## VIA, RTC, ADB, And Audio

- VIA IFR bit 1 is the Mac Plus CA1 vertical-blank source, not 6522 Timer 1 (bit 6). Generate CA1 at 60.15 Hz so ROM VBL cursor, button, keyboard, and task-queue processing runs.
- VIA input pins must remain separate from output latches and be combined according to DDR on reads. In particular, PB3 is the active-low mouse button input; losing it makes the ROM treat the mouse as held and deliberately eject boot media.
- Preserve 6522 timer acknowledgement semantics: reading T1C-L clears IFR6 and reading T2C-L clears IFR5. Timer-driven guest services otherwise leave IRQ asserted and can trap Finder in an interrupt storm.
- VIA Timer 1/2 flags come from timer state, not from IFR reads. An IFR read path that fabricates Timer 1 on every read starves the ROM's Timer 2 callback and hangs the `.Sony` motor/seek wait at PC `0x402424`.
- Low-memory `Ticks` advances only through the ROM's CA1 VBL handler. Never synthesize time on reads; doing so breaks debounce, double-click timing, delays, and event timestamps.
- Mac Plus RTC/PRAM traffic is bit-banged through VIA PB0 (data), PB1 (clock), and active-low PB2 (enable). Supply a valid Macintosh-epoch clock and PRAM signature; returning a floating-high `0xffffffff` date causes Alarm Clock to block Finder while normalizing the invalid date.
- IIcx ADB uses the external PIC-style transceiver between VIA1 PB3-PB5 and CB1/CB2. Valid response bytes leave PB3 high; only end-of-frame/error/SRQ lowers PB3. External-clock transmit completion must raise the VIA shift-register interrupt exactly once, with the command byte allowed to be staged before the S0 transition.
- Mac Plus audio scans 370 samples at 22.255 kHz from the high byte of each 16-bit RAM word. The main buffer begins at `RAMSize-$300` and the alternate buffer at `RAMSize-$5F00`; VIA PA3 selects between them. Honor active-low sound enable and the hardware volume curve (levels 0-6 attenuated, 7 full scale), and publish frontend-neutral signed 16-bit PCM. `EmulatorWindow` consumes the same reusable machine audio contract through Qt Multimedia; debug capture/export remains separately opt-in.
- The IIcx Apple Sound Chip consumes its FIFO at 22.257 kHz and raises half-empty FIFO interrupts through the inverted VIA2 CB1 input. Reading ASC FIFO status acknowledges both the status bits and IRQ. A register-array-only ASC leaves Sound Manager waiting forever during the System 7 unclean-shutdown warning and makes the dialog appear to crash the machine.

## Machine-Specific

### Mac Plus

- The v3 `skip_ram_pattern_test` patch skips only the destructive pattern passes. It preserves ROM checksum verification and RAM sizing, and updates the ROM's stored checksum to account for the branch edit.
- Until the machine has a cycle-callback/event scheduler, `MacPlusMachine::runCycles` must advance VIA timing after every instruction. Coarse CPU-only slices let ROM polling observe frozen devices and can strand `.Sony` timer waits.
- `MacPlusMachine` bus trace and sound capture default to disabled and should be enabled only by debug tooling or explicit tests.
- Host input must enter through ROM-visible hardware semantics. Mouse positions feed the SCC-produced `MTemp` accumulator without pre-updating `RawMouse`, button state changes only PB3 so VBL can post transitions, and keyboard transitions are returned through VIA SR inquiry responses rather than written into `KeyMap`.
- Retain a mouse-button edge for one VBL so the ROM observes it, but do not stretch ordinary clicks across several VBLs because classic controls interpret that as auto-repeat.

### Macintosh IIcx

- The `skip_ram_pattern_test` patch is independently SHA-256/byte gated for ROM checksum `0x97221136`. It replaces the destructive-pattern routine entry with its native `JMP (A6)` return and adjusts the stored ROM checksum; RAM sizing and mapping tests remain intact.
- GLUE RAM mapping must model bank-A/bank-B mirrors selected by VIA2 PA6/PA7. The `0x97221136` ROM deliberately accesses the mirror immediately above physical RAM during sizing; a flat bounded RAM array produces Sad Mac code `0x20005` for an 8 MiB configuration.
- The unmodified ROM plus optional in-memory RAM-test patch boots both System 6.0.8 System Tools and Utilities 1 raw 800K images to Finder with CuteMac Video at 640x480 one-bit startup mode. Keep headless framebuffer evidence under ignored `work/` during regressions.

### Quadra 800

- Reuse ADB endpoints, SWIM/floppy, SCC, RTC/PRAM, SCSI bus and targets, and suitable audio components while keeping its 68040 bus glue, interrupt map, onboard video, and SCSI controller behavior machine-specific.

### Quadra 700

- The 68040 reset vectors live in low RAM but point to the high ROM at `$40800000`; retain the 1 GiB low-RAM aperture and dirty-24-bit RAM aliases used by the ROM's memory manager and sizing code.
- The Q700 ROM's VIA1 Timer 2 calibration needs the same exact-write-sequence compatibility workaround as QEMU. Keep it in Q700 chipset glue, not the reusable VIA implementation, and write the calibrated `TimeDBRA`/`TimeSCCDB` values only after the ROM sequence.
- The optional, checksum- and byte-gated `quadra700.skip_ram_pattern_test` patch replaces only the destructive full-RAM routine entry with `MOVEQ #0,D6; JMP (A6)` and adjusts the ROM checksum header. It is a bringup aid and remains disabled by default.
