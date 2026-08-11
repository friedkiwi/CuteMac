# CuteMac Hardware And System Software Support

This document is the **scope map** for CuteMac: every Apple Macintosh machine from
the 1984 Macintosh 128K up to and including the pre-PCI (NuBus/PDS) Power
Macintosh generation, the peripherals those machines used, and every Apple
system software release before Mac OS X — plus, for each item, where CuteMac's
implementation actually stands today.

It is deliberately larger than what is built. Treat the tables as the long-term
target set, not a promise. The currently committed development targets are still
the narrower set named in `AGENTS.md`: 68000-era compacts, Macintosh IIcx,
Quadra 700/800, and the Power Macintosh 8100/80.

Related documents:

- `README.md` — build and run instructions
- `AGENTS.md` — working practices and architecture rules
- `docs/hardware-notes.md` — device-level bringup findings that constrain implementations

## Scope Boundary

"Pre-PCI" is the cut line. Every Macintosh whose expansion bus is NuBus, a
processor-direct slot (PDS), or nothing at all is in scope. The first PCI Macs
(Power Macintosh 7200/7500/8500/9500, February 1995 onward) and everything after
them are out of scope, as is Mac OS X and its Rhapsody/Darwin ancestry.

This means the Power Macintosh line is included only through its first, NuBus
generation: the 6100/7100/8100 family, their Performa and Workgroup Server
rebadges, the 603-based x200 machines, and the pre-PCI PowerBooks.

## Status Legend

| Badge | Meaning |
| --- | --- |
| 🟢 **Good** | Implemented and exercised — boots unmodified ROM/system software or is covered by CTest device-protocol tests. Known to work for its documented use. |
| 🟡 **Mostly working** | Implemented and usable, with known gaps, unvalidated corners, or missing modes documented in the notes column. |
| 🟠 **WIP** | Bringup in progress. Code exists in the tree but the device or machine does not yet reach a usable state on its own. |
| 🔴 **TODO** | Not implemented. No code in the tree beyond, at most, a catalog entry or an interface placeholder. |

Status is assessed against the tree as of this document's last update. Where a
row covers a family of hardware, the badge reflects the weakest member unless
the notes say otherwise.

---

## 1. Machines

### 1.1 Compact 68000 Macintosh

All-in-one 9" monochrome 512×342 machines. No expansion bus except the
Macintosh SE's PDS.

| Model | Year | CPU | Required devices | Optional / add-in | Status |
| --- | --- | --- | --- | --- | --- |
| Macintosh 128K | 1984 | 68000 @ 7.83 MHz | VIA 6522, Z8530 SCC, IWM (400K SS), compact video, compact sound, RTC/PRAM, [Mac keyboard + quadrature mouse](#31-pre-adb-input-macintosh-128k--plus) | External 400K floppy drive | 🟡 Mostly working |
| Macintosh 512K | 1984 | 68000 | as 128K | External 400K/800K drive | 🟡 Mostly working |
| Macintosh 512Ke | 1986 | 68000 | as 512K, 800K DS IWM, 128K ROM | External 800K drive, [Apple HD20](#37-floppy-port-and-external-drives) | 🟡 Mostly working |
| Macintosh Plus | 1986 | 68000 | as 512Ke + [SCSI (NCR 5380)](#33-scsi-shared-bus) | External drive, HD20, SCSI disks, SCSI-to-Ethernet | 🟢 Good |
| Macintosh SE | 1987 | 68000 | VIA, SCC, IWM (later SWIM), SCSI, [ADB](#32-apple-desktop-bus-shared-bus), compact video/sound, RTC/PRAM | SE PDS card (accelerator, Ethernet, video), second internal floppy or internal HD | 🔴 TODO |
| Macintosh SE FDHD | 1989 | 68000 | as SE with SWIM + SuperDrive | as SE | 🔴 TODO |
| Macintosh Classic | 1990 | 68000 @ 8 MHz | as SE FDHD, no PDS | Internal HD | 🔴 TODO |

**Implementation notes.** `MacPlusMachine` backs the Plus and, through its
`Model` enum, the 128K/512K/512Ke variants. The Plus is the most complete machine
in the tree: it boots unmodified ROMs to Finder, with IWM/GCR floppy, NCR 5380
SCSI, VIA/RTC/PRAM, compact video and the 22.255 kHz sound buffer all working.
The earlier three models share that machine class but have had far less exposure;
their ROM-specific behaviour, memory sizing, and lack of SCSI are not
independently validated. The SE and Classic have no machine class at all — they
need the ADB transceiver wired to a compact-Mac chassis plus an SE PDS model.

### 1.2 Compact 68030 Macintosh

| Model | Year | CPU | Required devices | Optional / add-in | Status |
| --- | --- | --- | --- | --- | --- |
| Macintosh SE/30 | 1989 | 68030 + 68882 | VIA×2, SCC, SWIM, SCSI, ADB, ASC, RTC/PRAM, compact video | 030 PDS card (video, Ethernet, accelerator), internal HD | 🔴 TODO |
| Macintosh Classic II / Performa 200 | 1991 | 68030 @ 16 MHz | VIA, SCC, SWIM, SCSI, ADB, ASC, RTC/PRAM, compact video | Internal HD, FPU | 🔴 TODO |
| Macintosh Color Classic / Performa 250 | 1993 | 68030 | as Classic II + 512×384 built-in colour video | LC PDS card, FPU | 🔴 TODO |
| Macintosh Color Classic II / Performa 275 | 1993 | 68030 @ 33 MHz | as Color Classic | LC PDS card | 🔴 TODO |

The SE/30 is the highest-value machine in this group: it is a Macintosh IIx in a
compact case, so it reuses almost everything the IIcx work already produced
except NuBus. Its 030 PDS carries the same video and Ethernet cards as the SE
PDS in electrically different form.

### 1.3 Modular 68020/68030 Macintosh (NuBus)

| Model | Year | CPU | Required devices | Optional / add-in | Status |
| --- | --- | --- | --- | --- | --- |
| Macintosh II | 1987 | 68020 + 68881 | VIA×2, SCC, IWM (SWIM after FDHD upgrade), SCSI, ADB, ASC, RTC/PRAM, **[NuBus video card](#34-nubus-shared-bus)** | 68851 PMMU, 6 NuBus slots, FDHD upgrade | 🔴 TODO |
| Macintosh IIx | 1988 | 68030 | as II, SWIM standard | 6 NuBus slots | 🔴 TODO |
| Macintosh IIcx | 1989 | 68030 @ 16 MHz | VIA×2, SCC, SWIM1, NCR 5380 + GLUE, ADB, ASC, RTC/PRAM, NuBus video card | 3 NuBus slots | 🟢 Good |
| Macintosh IIci | 1989 | 68030 @ 25 MHz | as IIcx + **onboard RBV/VDAC video** | Cache slot, 3 NuBus slots | 🔴 TODO |
| Macintosh IIfx | 1990 | 68030 @ 40 MHz | VIA×2, **IOP-driven SCC and SWIM**, OSS interrupt controller, SCSI, ADB, ASC, RTC/PRAM, NuBus video | 6 NuBus slots | 🔴 TODO |
| Macintosh IIsi | 1990 | 68030 @ 20 MHz | VIA, SCC, SWIM, SCSI, **Egret** (ADB/RTC/power), onboard video, ASC | One PDS **or** NuBus slot via adapter, FPU | 🔴 TODO |
| Macintosh IIvi / IIvx | 1992 | 68030 @ 16/32 MHz | VIA, SCC, SWIM, SCSI, Egret, onboard video, ASC | 3 NuBus slots, internal CD-ROM | 🔴 TODO |

**Implementation notes.** The IIcx is the reference NuBus machine and boots
System 6.0.8 to Finder from unmodified ROM with either the authentic Apple
Macintosh II Video Card or the project's own CuteMac Video card. The IIfx is the
hardest machine in this table by a wide margin — its I/O Processors and OSS
interrupt controller share almost nothing with the rest of the family. The IIsi
and IIvx introduce Egret, which is the direct ancestor of the Cuda controller
already modelled for the Power Macintosh.

### 1.4 Macintosh LC And Performa (LC PDS)

| Model | Year | CPU | Required devices | Optional / add-in | Status |
| --- | --- | --- | --- | --- | --- |
| Macintosh LC | 1990 | 68020 @ 16 MHz | VIA, SCC, SWIM, SCSI, Egret, V8 onboard video, ASC | **LC PDS**: [Apple IIe Card](#36-processor-direct-slot-pds-per-machine), Ethernet, video | 🔴 TODO |
| Macintosh LC II / Performa 400-430 | 1992 | 68030 @ 16 MHz | as LC | LC PDS, FPU | 🔴 TODO |
| Macintosh LC III / Performa 450 | 1993 | 68030 @ 25 MHz | VIA, SCC, SWIM2, SCSI, Egret, **Sonora** video, ASC | LC PDS, FPU | 🔴 TODO |
| Macintosh LC 520 / 550 / Performa 5xx | 1993 | 68030 | as LC III, built-in 14" colour display, CD-ROM | LC PDS | 🔴 TODO |
| Macintosh LC 475 / Quadra 605 / Performa 475 | 1993 | 68LC040 | VIA, SCC, SWIM2, NCR 53C94 SCSI, Egret, Sonora-class video, ASC | LC PDS, full 68040 upgrade | 🔴 TODO |
| Macintosh LC 575 / Performa 575-578 | 1994 | 68LC040 | as LC 475, built-in display, CD-ROM | LC PDS, Ethernet | 🔴 TODO |
| Macintosh LC 630 / Quadra 630 / Performa 630 | 1994 | 68040 / 68LC040 | VIA, SCC, SWIM3, **IDE + SCSI**, Egret/Cuda, Valkyrie video, ASC | LC PDS, comm slot, video-in, TV tuner, CD-ROM | 🔴 TODO |

The 630 family is the only 68k Mac line with an **IDE** boot disk, which means a
whole storage controller CuteMac does not have. It is also the machine family
with the richest optional-card story (TV tuner, video input, communication slot),
so it is a poor early target despite being late and common.

### 1.5 Quadra And Centris (68040)

| Model | Year | CPU | Required devices | Optional / add-in | Status |
| --- | --- | --- | --- | --- | --- |
| Quadra 700 | 1991 | 68040 @ 25 MHz | VIA×2, SCC, SWIM, NCR 53C96 SCSI, ADB, **DAFB onboard video**, ASC, RTC/PRAM, **SONIC Ethernet + AAUI** | 2 NuBus slots, 68040 PDS | 🟠 WIP |
| Quadra 900 | 1991 | 68040 @ 25 MHz | as Q700 + **IOP-driven SCC/SWIM** | 5 NuBus slots, tower drive bays | 🔴 TODO |
| Quadra 950 | 1992 | 68040 @ 33 MHz | as Quadra 900 | 5 NuBus slots | 🔴 TODO |
| Centris 610 / Quadra 610 | 1993 | 68LC040 / 68040 | VIA×2, SCC, SWIM2, 53C94 SCSI, ADB, DAFB-class video, ASC, Ethernet | 1 NuBus (via adapter) or PDS, DOS card | 🔴 TODO |
| Centris 650 / Quadra 650 | 1993 | 68040 | as Centris 610 | 3 NuBus slots, Ethernet | 🔴 TODO |
| Quadra 800 | 1993 | 68040 @ 33 MHz | VIA×2, SCC, SWIM2, 53C94 SCSI, ADB, DAFB II video, ASC, SONIC Ethernet | 3 NuBus slots, 68040 PDS | 🔴 TODO |
| Centris 660AV / Quadra 660AV | 1993 | 68040 @ 25 MHz | VIA, **Curio** (SCC/SCSI/SONIC), **DSP3210**, Cuda, video in/out, GeoPort, ASC/Singer audio | 1 NuBus/PDS, CD-ROM | 🔴 TODO |
| Quadra 840AV | 1993 | 68040 @ 40 MHz | as 660AV @ higher clock | 3 NuBus slots, CD-ROM | 🔴 TODO |

**Implementation notes.** `Quadra700Machine` exists and has focused bringup
coverage: 68040 reset vectoring from low RAM into high ROM, RAM mapping without
low-memory aliases, VIA2 Timer 1 PB7 chained into VIA1 CA1 for VBL, and the
QEMU-compatible Timer 2 calibration workaround. `DafbVideo` is implemented with
its own test. It does not yet boot system software, and its onboard SONIC
Ethernet is not modelled — networking on the Quadra 700 today means adding a
NuBus Ethernet card, which is historically legal but not how the machine shipped.

Quadra 800 is listed in `MachineCatalog` with RAM sizes but has **no machine
class and no factory branch** in `EmulationSession::createMachine`, so selecting
it produces no session. That catalog entry is a placeholder, not support.

The AV machines are their own project: the DSP3210, Curio, and video digitizer
have no analogue anywhere else in the line.

### 1.6 PowerBook And Duo (68k)

| Model | Year | CPU | Required devices | Optional / add-in | Status |
| --- | --- | --- | --- | --- | --- |
| Macintosh Portable | 1989 | 68000 @ 16 MHz | VIA, SCC, IWM/SWIM, SCSI, ADB, **power manager**, active-matrix 640×400 LCD, lead-acid battery | Internal modem, RAM/ROM expansion, second floppy | 🔴 TODO |
| PowerBook 100 | 1991 | 68000 @ 16 MHz | as Portable, no internal floppy, **SCSI disk mode** | External floppy, internal modem | 🔴 TODO |
| PowerBook 140 / 145 / 145B / 170 | 1991-92 | 68030 | VIA, SCC, SWIM, SCSI, ADB, power manager, LCD, sound | Internal modem, RAM card | 🔴 TODO |
| PowerBook 160 / 165 / 165c / 180 / 180c | 1992-93 | 68030 | as above + **external video out** | Internal modem, RAM card | 🔴 TODO |
| PowerBook 150 | 1994 | 68030 | as above, **IDE disk** | RAM card | 🔴 TODO |
| PowerBook Duo 210 / 230 / 250 / 270c / 280 / 280c | 1992-94 | 68030 | VIA, SCC, SWIM, ADB, power manager, LCD, **Duo dock connector** | Duo Dock / MiniDock / Dock II, internal modem | 🔴 TODO |
| PowerBook 500 series (520/520c/540/540c) | 1994 | 68LC040 | VIA, SCC, SWIM2, SCSI, ADB, power manager, **trackpad**, built-in Ethernet, stereo sound | PC Card cage, PowerPC upgrade card, second battery | 🔴 TODO |
| PowerBook 190 / 190cs | 1995 | 68LC040 | as 500 series, **IDE disk**, PC Card | PC Card modem/Ethernet | 🔴 TODO |

Portables add an entire subsystem CuteMac has never touched: the **Power
Manager** microcontroller, which owns sleep, battery, backlight, the trackball/
trackpad, and on most models the ADB bus itself. No PowerBook is realistically
reachable without it.

### 1.7 Power Macintosh — NuBus / Pre-PCI

The first PowerPC generation. All use the PowerPC 601, the HMC memory
controller, Cuda for ADB/RTC/PRAM/power, and Curio for SCC/SCSI/Ethernet.

| Model | Year | CPU | Required devices | Optional / add-in | Status |
| --- | --- | --- | --- | --- | --- |
| Power Macintosh 6100/60, /60AV, /66 | 1994 | PPC 601 @ 60-66 MHz | HMC, AMIC, Cuda, Curio (SCC + 53C94 + SONIC/AAUI), SWIM2, onboard video, AWACS/Singer audio | **1 PDS**: HPV video card, AV card, [DOS Compatibility Card](#36-processor-direct-slot-pds-per-machine), NuBus adapter; CD-ROM | 🔴 TODO |
| Power Macintosh 7100/66, /80, AV | 1994 | PPC 601 @ 66-80 MHz | as 6100 | **3 NuBus + PDS**: HPV/AV video card; CD-ROM | 🔴 TODO |
| Power Macintosh 8100/80, /100, /110, AV | 1994-95 | PPC 601 @ 80-110 MHz | HMC, **BART** NuBus controller, Cuda, Curio, SWIM2, video, audio | **3 NuBus + PDS**: HPV/AV video card; CD-ROM, second internal disk | 🟠 WIP |
| Performa 6110CD-6118CD | 1994-95 | PPC 601 @ 60 MHz | as 6100 | CD-ROM standard, TV tuner, modem | 🔴 TODO |
| Apple Workgroup Server 6150 / 8150 / 9150 | 1994-95 | PPC 601 | as 6100 / 8100 respectively, server-configured SCSI and RAM | DAT tape, external RAID, [AppleShare](#63-server-and-workgroup-software) | 🔴 TODO |
| Power Macintosh / Performa 5200, 6200 (x200) | 1995 | PPC 603 @ 75 MHz | 603 on a **Quadra-derived 68k-style bus**, Valkyrie video, Cuda, SWIM3, IDE + SCSI | LC PDS, comm slot, TV tuner, CD-ROM | 🔴 TODO |
| Power Macintosh / Performa 5300, 6300 | 1995 | PPC 603e @ 100 MHz | as x200 | as x200 | 🔴 TODO |
| PowerBook 5300 series | 1995 | PPC 603e | power manager, PC Card, IDE, ADB, SCSI, LCD | PC Card modem/Ethernet, video out | 🔴 TODO |
| PowerBook Duo 2300c | 1995 | PPC 603e | power manager, Duo connector, IDE, ADB | Duo Dock, modem | 🔴 TODO |

**Implementation notes.** `PowerMac8100Machine` is the active PowerPC bringup.
It has a real physical memory map with HMC, AMIC, NuBus and machine-ID regions,
a `Cuda` controller, dual `Ncr53c94` SCSI controllers with a DMA path, VIA1 plus
a VIA2-style interrupt block, `Z8530Scc`, and a `SonoraVideo`-class framebuffer
standing in for the machine's video path. `PowerPc601Core` is a portable
interpreter with BAT/page translation, exception handling, and trace rings, and
has its own CTest coverage. What it does not have: a modelled HPV/AV PDS video
card, AWACS/Singer audio (ASC accesses are absorbed by a compatibility shim),
SONIC Ethernet, floppy support (`loadFloppyImage` returns `false`), and guest
input (`queueInput` is empty, so there is no keyboard or mouse yet).

The x200 machines are architecturally closer to a Quadra than to an 8100 despite
being PowerPC, and the PowerBooks again need the power manager.

---

## 2. Machine Support Summary

| Machine | Reaches | Notes |
| --- | --- | --- |
| Macintosh Plus | 🟢 Finder | Reference machine. Floppy, SCSI, sound, PRAM, serial all working. |
| Macintosh 128K / 512K / 512Ke | 🟡 Boots | Shares `MacPlusMachine`; per-model ROM behaviour not independently validated. |
| Macintosh IIcx | 🟢 Finder | Boots System 6.0.8 from unmodified ROM with NuBus video, ADB, SWIM1, SCSI, ASC. |
| Quadra 700 | 🟠 ROM bringup | 68040 reset, RAM map, VIA VBL chain, DAFB. Does not boot system software. |
| Quadra 800 | 🔴 Catalog only | Listed in `MachineCatalog`, no machine class, cannot be instantiated. |
| Power Macintosh 8100/80 | 🟠 ROM bringup | 601 interpreter, HMC/AMIC/BART map, Cuda, 53C94, Sonora framebuffer. No input, no floppy, no audio. |
| Everything else above | 🔴 TODO | No machine class. |

---

## 3. Shared Buses And Adapters

These are the cross-cutting interfaces. A peripheral row elsewhere in this
document that names a bus is subject to that bus's status as well as its own.

### 3.1 Pre-ADB Input (Macintosh 128K – Plus)

| Item | Attaches via | Required? | Status | Notes |
| --- | --- | --- | --- | --- |
| Macintosh keyboard (M0110/M0110A) | RJ-11, VIA shift register | Required in practice | 🟢 Good | Delivered as VIA SR inquiry responses, not by writing `KeyMap`. |
| Macintosh Numeric Keypad (M0120) | Daisy-chained to keyboard | Optional | 🔴 TODO | |
| Macintosh mouse (M0100) | DE-9 quadrature → VIA/SCC | Required in practice | 🟢 Good | Motion feeds the SCC-produced `MTemp` accumulator; button is VIA PB3 active-low. |

### 3.2 Apple Desktop Bus (Shared Bus)

Present on every Mac from the Macintosh SE and Macintosh II forward. Physically
identical across machines; the *host side* differs — a VIA-attached PIC-style
transceiver on the II family, Egret on the IIsi/LC/Quadra generation, Cuda on the
Power Macintosh, and the power manager on portables.

| Item | Required? | Status | Notes |
| --- | --- | --- | --- |
| ADB host: VIA + external transceiver (Mac II family) | Required | 🟡 Mostly working | `AdbTransceiver`, validated against IIcx ROM ordering; has CTest coverage. |
| ADB host: Egret (IIsi, LC, IIvx, early Quadra) | Required | 🔴 TODO | Direct ancestor of Cuda; should share its packet layer. |
| ADB host: Cuda (Power Macintosh, later Quadra) | Required | 🟠 WIP | `CudaController` handles packets and PRAM for 8100 bringup; not driving input yet. |
| ADB host: Power Manager (all portables) | Required | 🔴 TODO | |
| Apple Keyboard (M0116) / Apple Keyboard II | Required in practice | 🟡 Mostly working | Default handler `0x22` at address 2. |
| Apple Extended Keyboard / Extended Keyboard II | Optional | 🟡 Mostly working | Modelled as the same endpoint; extended-only keys unverified. |
| Apple Adjustable Keyboard | Optional | 🔴 TODO | |
| AppleDesign Keyboard | Optional | 🔴 TODO | Behaves as a standard endpoint; untested. |
| Apple Desktop Bus Mouse (G5431) / ADB Mouse II | Required in practice | 🟡 Mostly working | Default handler `0x23` at address 3; relative motion clamped to ±64 per poll. |
| Kensington Turbo Mouse (popular) | Optional | 🔴 TODO | Extended handler with its own registers. |
| Logitech TrackMan / third-party trackballs (popular) | Optional | 🔴 TODO | Most work as plain mouse endpoints. |
| Wacom ADB graphics tablets (popular) | Optional | 🔴 TODO | |
| Gravis MouseStick / CH Products joysticks (popular) | Optional | 🔴 TODO | |
| ADB software-protection dongles (popular) | Optional | 🔴 TODO | Relevant for period software that will not launch without one. |
| ADB address reassignment / SRQ arbitration | Required | 🟡 Mostly working | Reassignment and SRQ are implemented; multi-device collision handling is thin. |

The absolute-pointer integration used by CuteMac Video deliberately **bypasses**
ADB: host coordinates go through the card's guest-services mailbox instead. Real
ADB mice keep relative semantics.

### 3.3 SCSI (Shared Bus)

Standard from the Macintosh Plus onward. Controller varies; the bus and the
targets do not.

| Item | Machines | Required? | Status | Notes |
| --- | --- | --- | --- | --- |
| NCR 5380 controller | Plus, SE, II, IIx, IIcx, IIci, IIsi, SE/30, LC | Required | 🟢 Good | Plus and IIcx paths both work, including Macintosh pseudo-DMA and blind transfers via `MacintoshNcr5380Bus`. |
| NCR 53C94 / 53CF94 ("Curio") controller | Quadra/Centris, LC 475+, Power Macintosh | Required | 🟠 WIP | Implemented and wired into the 8100 with a DMA path; not yet driving a booted system. |
| IIfx SCSI + DMA | IIfx only | Required | 🔴 TODO | |
| Fixed disks (Apple 20SC/40SC/80SC/160SC, Quantum, Conner, IBM) | All SCSI Macs | One boot disk required | 🟢 Good | `ScsiBlockDevice` derives its INQUIRY personality from image size and supports FORMAT UNIT, MODE SELECT/SENSE, VERIFY(10), and READ DEFECT DATA so Apple HD SC Setup and Drive Setup can prepare it. |
| CD-ROM (AppleCD SC/150/300/300i/600i, and later) | II family onward | Optional | 🟢 Good | `ScsiCdRomDevice`: 2048-byte ISO sectors, `MATSHITA CD-ROM CR-8004` identity, READ TOC, unit-attention on media change, runtime insert/eject from the session toolbar. |
| Removable cartridges — SyQuest 44/88/200, Iomega Zip/Jaz, Bernoulli (popular) | All SCSI Macs | Optional | 🔴 TODO | Would extend `ScsiBlockDevice` with removable-media semantics already present in the CD-ROM target. |
| Magneto-optical drives | All SCSI Macs | Optional | 🔴 TODO | |
| Tape — Apple Tape Backup 40SC, DAT/DDS | Servers, Quadras | Optional | 🔴 TODO | Sequential-access device class not modelled. |
| Scanners — Apple Scanner, OneScanner, Color OneScanner; UMAX, Microtek (popular) | All SCSI Macs | Optional | 🔴 TODO | Scanner device class not modelled. |
| SCSI-to-Ethernet — Asanté EN/SC, Dayna DaynaPORT SCSI/Link (popular) | Compact Macs especially | Optional | 🔴 TODO | The standard way to network a Plus/SE/Classic. Would bridge to the existing `PacketNetworkBackend`. |
| SCSI disk mode (target mode) | PowerBook 100 and later portables | Optional | 🔴 TODO | |

### 3.4 NuBus (Shared Bus)

Macintosh II through the Power Macintosh 8100. Every NuBus card is **optional**,
except that machines without onboard video (Mac II, IIx, IIcx, IIfx) require at
least one video card to be usable.

| Card | Machines | Status | Notes |
| --- | --- | --- | --- |
| Apple Macintosh II Video Card, 630-0153 ("Toby") | II family | 🟢 Good | Authentic `342-0008-A` declaration ROM mapped as the physical lane-0 device, 512 KiB VRAM, Bt453 palette, 640×480 at 1/2/4/8-bit, slot interrupts. |
| Apple Macintosh Display Card 8•24 | II family, Quadra | 🟡 Mostly working | Includes monitor sense-line selection; 24-bit direct colour and higher resolutions are the least-tested area. |
| Apple Macintosh Display Card 8•24 GC (AMD 29000 accelerated) | II family, Quadra | 🔴 TODO | |
| Apple Two-Page Monochrome / Portrait Video Card | II family | 🔴 TODO | |
| **CuteMac Video** (project-owned, non-historical) | Any NuBus machine | 🟢 Good | Own declaration ROM and 68k driver built from `cutemac_video_driver.S` via Retro68. Configurable dimensions/depth, guest-programmable RAMDAC, dirty-region reporting, optional integrated absolute pointer. |
| **CuteMac Video Accelerated** (project-owned) | Any NuBus machine | 🟠 WIP | Separate adapter delegating to the known-good card; host-accelerated fill/copy/expand/cursor is the development track. |
| Apple NuBus Ethernet Card (`aenet1`, 670-0210) | II family, Quadra | 🟡 Mostly working | DP8390 with local packet RAM; host connectivity only through `PacketNetworkBackend`. Has CTest coverage. |
| Apple Ethernet NB / EtherTalk NB | II family, Quadra | 🔴 TODO | |
| Apple Token Ring 4/16 NB | II family, Quadra | 🔴 TODO | |
| Apple Serial NB | II family | 🔴 TODO | |
| Third-party Ethernet — Asanté MacCon, Farallon, Dayna (popular) | II family, Quadra | 🔴 TODO | |
| Third-party video — Radius PrecisionColor/Pivot, RasterOps, SuperMac Spectrum/Thunder (popular) | II family, Quadra | 🔴 TODO | |
| Radius Rocket / accelerator cards (popular) | II family, Quadra | 🔴 TODO | Second CPU on a card; a large architectural question, not a device. |
| Video capture — Radius VideoVision, SuperMac (popular) | Quadra | 🔴 TODO | |
| DOS-on-a-card — Orange Micro Mac86/Mac286 (popular) | II family | 🔴 TODO | |
| NuBus bus and slot decoding | II family through 8100 | 🟢 Good | `NuBusBus` with correct byte-lane behaviour, sparse declaration-ROM views, slot interrupts, and 24-bit logical slot window translation. |

### 3.5 Serial / SCC (Shared Bus)

Two Z8530 channels — modem port (A) and printer port (B) — on every machine in
scope.

| Item | Required? | Status | Notes |
| --- | --- | --- | --- |
| Z8530 SCC | Required | 🟡 Mostly working | Register protocol has CTest coverage; RR1 and some status bits are stubbed, and LocalTalk framing is an idle-wire stub by design. |
| ImageWriter I / II / LQ | Optional | 🟡 Mostly working | `ImageWriterII` renders to sequential PNG pages through an interchangeable sink. ImageWriter I and LQ variants not modelled. |
| StyleWriter / StyleWriter II | Optional | 🔴 TODO | |
| LaserWriter (serial + LocalTalk) | Optional | 🔴 TODO | Needs LocalTalk and PostScript-side handling, or a print-to-file bridge. |
| Hayes-compatible modem (Apple Personal Modem, third-party) | Optional | 🟢 Good | `HayesModem` with phonebook dialling, optional direct `host:port` TCP, telnet negotiation filtering, and SLIP/PPP networking through libslirp. |
| Terminal / null modem | Optional | 🟢 Good | `NullModem` in TCP listen or dial mode for debuggers and null-modem workflows. |
| LocalTalk / PhoneNet networking | Optional | 🔴 TODO | Kept behind a controller-independent endpoint boundary; the default is an unattached idle wire. |
| GeoPort / GeoPort Telecom Adapter | Optional | 🔴 TODO | AV and PowerPC machines only. |
| MIDI interfaces (popular) | Optional | 🔴 TODO | |
| Serial Newton / PDA connection (popular) | Optional | 🔴 TODO | |

### 3.6 Processor-Direct Slot (PDS, Per-Machine)

Unlike NuBus, PDS is **not** a shared adapter — it is a different connector and a
different electrical contract on nearly every machine. Cards are always optional.

| Slot | Machines | Typical cards | Status |
| --- | --- | --- | --- |
| SE PDS | Macintosh SE | Accelerators, Ethernet (Asanté MacCon SE), video adapters | 🔴 TODO |
| 030 Direct Slot | SE/30, IIfx | Ethernet, video, accelerators, Micron Xceed grayscale | 🔴 TODO |
| LC PDS | LC family, Color Classic, x200 | **Apple IIe Card**, Apple Ethernet LC, video, TV tuner | 🔴 TODO |
| IIsi PDS | IIsi | NuBus adapter card, Ethernet, FPU adapter | 🔴 TODO |
| 68040 PDS | Quadra 700/800/900/950 | Ethernet, video, **PowerPC Upgrade Card** | 🔴 TODO |
| PPC 601 PDS | Power Mac 6100/7100/8100 | **HPV video card**, **AV card**, DOS Compatibility Card (Houdini), NuBus adapter (6100) | 🔴 TODO |
| Comm slot | LC 575, 630 family, x200 | Ethernet, internal modem | 🔴 TODO |
| PC Card / PCMCIA | PowerBook 500 (cage), 190, 5300 | Modem, Ethernet, storage | 🔴 TODO |

The **HPV video card** matters more than most PDS entries: it is how a real
7100 and 8100 produce video at all. CuteMac's 8100 currently stands in a
Sonora-class onboard framebuffer instead.

### 3.7 Floppy Port And External Drives

| Item | Machines | Required? | Status | Notes |
| --- | --- | --- | --- | --- |
| IWM controller | 128K – Plus, SE, Mac II | Required | 🟢 Good | Soft-switch ordering validated against unmodified ROM; CTest coverage. |
| SWIM / SWIM1 | SE FDHD, IIx onward | Required | 🟡 Mostly working | ISM mode entry via `0x40,0x00,0x40,0x40`; GCR and MFM paths both implemented for the IIcx. |
| SWIM2 / SWIM3 | Quadra, LC III+, Power Macintosh | Required | 🔴 TODO | The 8100 has no floppy support at all today. |
| IOP-driven floppy | IIfx, Quadra 900/950 | Required | 🔴 TODO | |
| 400K single-sided GCR media | 128K, 512K | Required (boot media) | 🟢 Good | Raw and Disk Copy 4.2 images. |
| 800K double-sided GCR media | 512Ke onward | Required (boot media) | 🟢 Good | Raw and Disk Copy 4.2 images. |
| 1.44 MB MFM SuperDrive media | SE FDHD onward | Optional | 🟢 Good | 80-track, two-sided, 18-sector MFM through SWIM1 ISM with CRC fields and high-density sensing. |
| Floppy writes | All | Optional | 🟡 Mostly working | Write windows are committed back to the backing image; MFM sector inference is heuristic. |
| External 400K/800K drive (M0130/M0131, Apple 3.5 Drive) | 128K – SE | Optional | 🔴 TODO | Second drive exists in the model; the *external* drive-port semantics do not. |
| Apple HD20 (hard disk on the floppy port) | 512Ke, Plus | Optional | 🔴 TODO | Non-SCSI, protocol driven over the disk port. |
| Apple FDHD external / Apple SuperDrive | SE FDHD onward | Optional | 🔴 TODO | |

### 3.8 Onboard Video (Per-Machine)

Machines from the IIci onward mostly have video on the logic board, which means
each generation is its own device rather than a shared card.

| Item | Machines | Status | Notes |
| --- | --- | --- | --- |
| Compact 512×342 monochrome video | 128K – Classic, SE/30, Classic II | 🟢 Good | Working for the Plus-family machines in the tree. |
| RBV / VDAC | IIci, IIsi | 🔴 TODO | |
| V8 / Sonora | LC, LC II, LC III, IIvx | 🟠 WIP | `SonoraVideo` exists but is currently used as the 8100's stand-in framebuffer, not as an LC-family device. |
| DAFB / DAFB II | Quadra 700/800/900/950, Centris | 🟠 WIP | `DafbVideo` implemented with CTest coverage; not yet driving a booted system. |
| Valkyrie | 630 family, x200 | 🔴 TODO | |
| Civic / AV video with digitizer | 660AV, 840AV | 🔴 TODO | |
| Built-in colour LCD panels | PowerBooks, Color Classic, all-in-ones | 🔴 TODO | |

### 3.9 Sound

| Item | Machines | Status | Notes |
| --- | --- | --- | --- |
| Compact-Mac PWM sound buffer | 128K – Classic | 🟢 Good | 370 samples at 22.255 kHz from RAM high bytes, dual buffers selected by VIA PA3, hardware volume curve, active-low enable. |
| Apple Sound Chip (ASC) | Mac II family, Quadra | 🟡 Mostly working | FIFO consumed at 22.257 kHz with half-empty interrupts through inverted VIA2 CB1; enough for Sound Manager. CTest coverage. |
| Enhanced ASC / Singer / AWACS | AV Quadras, Power Macintosh | 🔴 TODO | 8100 ASC-range accesses are absorbed by a compatibility shim, not emulated. |
| Sound input (Apple PlainTalk microphone) | 660AV onward, Power Macintosh | 🔴 TODO | |
| DSP3210 | 660AV, 840AV | 🔴 TODO | |

### 3.10 Networking

| Item | Machines | Required? | Status | Notes |
| --- | --- | --- | --- | --- |
| LocalTalk (built into the printer port) | All | Optional | 🔴 TODO | Idle-wire stub by design; endpoint boundary exists for a future bridge. |
| SONIC Ethernet + AAUI (onboard) | Quadra 700/800/610/650, PowerBook 500, Power Macintosh | Optional | 🔴 TODO | Notably absent from the Quadra 700, where it shipped standard. |
| MACE Ethernet | Some Quadra/Centris | Optional | 🔴 TODO | |
| AAUI transceivers (10BASE-T, thin coax) | Any AAUI machine | Optional | 🔴 TODO | |
| NuBus Ethernet | See [3.4](#34-nubus-shared-bus) | Optional | 🟡 Mostly working | Currently the only working Ethernet path in the emulator. |
| SCSI-to-Ethernet | See [3.3](#33-scsi-shared-bus) | Optional | 🔴 TODO | |
| Host bridge (`SlirpEthernetBackend`) | — | — | 🟡 Mostly working | User-mode networking via libslirp; also backs modem SLIP/PPP. |

### 3.11 Real-Time Clock, PRAM, And Power

| Item | Machines | Status | Notes |
| --- | --- | --- | --- |
| VIA bit-banged RTC + 20/256-byte PRAM | 128K – Mac II family | 🟢 Good | PB0 data, PB1 clock, active-low PB2 enable. Host-local time in the classic Macintosh epoch; guest time writes are discarded, PRAM changes persist. |
| Egret-managed RTC/PRAM | IIsi, LC, IIvx | 🔴 TODO | |
| Cuda-managed RTC/PRAM | Quadra (late), Power Macintosh | 🟠 WIP | `CudaController` holds a 256-byte PRAM and answers time requests. |
| Power Manager | All portables | 🔴 TODO | |
| Soft power on/off | IIsi onward | 🔴 TODO | `GuestPowerRequest` exists at the interface level. |

---

## 4. Displays

Displays attach through whichever video device the machine has, and are selected
by monitor **sense lines** — which is why they matter to the emulator at all.

| Display | Resolution | Required? | Status |
| --- | --- | --- | --- |
| Built-in compact 9" monochrome | 512×342 | Required on compacts | 🟢 Good |
| Apple Monochrome Monitor / High-Resolution Monochrome 12" | 640×480 | Optional | 🟡 Mostly working |
| Apple 12" RGB Display | 512×384 | Optional | 🔴 TODO |
| AppleColor High-Resolution RGB 13" | 640×480 | Optional | 🟢 Good |
| Apple Macintosh Color Display 14" | 640×480 | Optional | 🟡 Mostly working |
| Apple Two-Page Monochrome Display 21" | 1152×870 | Optional | 🔴 TODO |
| Apple Portrait Display 15" | 640×870 | Optional | 🔴 TODO |
| Apple Multiple Scan 15/17/20 | Multi-sync | Optional | 🔴 TODO |
| Apple AudioVision 14 (ADB + audio pass-through) | 640×480 | Optional | 🔴 TODO |
| VGA / SVGA via adapter (popular) | 640×480+ | Optional | 🔴 TODO |

Sense-line selection is implemented for the Apple Macintosh Display Card 8•24;
elsewhere the display is implied by the video device's configured mode rather
than chosen by a modelled monitor.

---

## 5. Printers, Scanners, And Other Peripherals

| Item | Attaches via | Required? | Status |
| --- | --- | --- | --- |
| ImageWriter II | [Serial](#35-serial--scc-shared-bus) | Optional | 🟡 Mostly working |
| ImageWriter I / ImageWriter LQ | Serial | Optional | 🔴 TODO |
| StyleWriter / StyleWriter II | Serial | Optional | 🔴 TODO |
| Personal LaserWriter / LaserWriter II | Serial or LocalTalk | Optional | 🔴 TODO |
| LaserWriter 8500 / networked PostScript | LocalTalk or EtherTalk | Optional | 🔴 TODO |
| Apple Scanner / OneScanner / Color OneScanner | [SCSI](#33-scsi-shared-bus) | Optional | 🔴 TODO |
| Third-party scanners — UMAX, Microtek, Nikon (popular) | SCSI | Optional | 🔴 TODO |
| AppleCD SC / 150 / 300 / 300i / 600i | SCSI | Optional | 🟢 Good |
| Apple Tape Backup 40SC, DAT | SCSI | Optional | 🔴 TODO |
| SyQuest / Iomega Zip / Bernoulli removables (popular) | SCSI | Optional | 🔴 TODO |
| Apple Personal Modem / third-party Hayes modems | Serial | Optional | 🟢 Good |
| Apple MIDI Interface, MIDI Time Piece (popular) | Serial | Optional | 🔴 TODO |
| Newton MessagePad (popular) | Serial | Optional | 🔴 TODO |
| Apple IIe Card | [LC PDS](#36-processor-direct-slot-pds-per-machine) | Optional | 🔴 TODO |
| DOS Compatibility Card (Houdini, 486) | PDS | Optional | 🔴 TODO |
| PowerPC Upgrade Card | 68040 PDS | Optional | 🔴 TODO |
| Apple Adjustable Keyboard, Turbo Mouse, tablets, joysticks | [ADB](#32-apple-desktop-bus-shared-bus) | Optional | 🔴 TODO |

---

## 6. System Software

CuteMac's obligation to system software is indirect: it does not implement any of
it, but each release exercises a distinct set of hardware behaviours, and the
status column below reflects **whether that release has been observed running**,
not whether the OS itself is supported.

### 6.1 System / Mac OS (Classic)

| Release | Year | Minimum hardware | Last machine generation supported | Status |
| --- | --- | --- | --- | --- |
| System 1.0 – 1.1 | 1984 | 68000, 128K, 400K floppy | Compact 68000 | 🔴 TODO |
| System 2.0 – 2.1 | 1985 | 68000 | Compact 68000 | 🔴 TODO |
| System 3.0 – 3.4 (HFS) | 1986 | 68000, 512Ke+ | Compact 68000, Mac II | 🔴 TODO |
| System 4.0 – 4.1 | 1987 | 68000 | Mac SE, Mac II | 🔴 TODO |
| System 6.0 – 6.0.4 | 1988-89 | 68000 | Mac II family | 🟡 Mostly working |
| **System 6.0.7 – 6.0.8** | 1990-91 | 68000 | Mac II family, Quadra 700 | 🟢 Good |
| System 7.0 / 7.0.1 | 1991 | 68000, 2 MB | Quadra | 🔴 TODO |
| System 7.1 / 7.1.1 Pro | 1992-93 | 68000, 2 MB | Quadra, AV | 🟠 WIP |
| System 7.1.2 | 1994 | **PowerPC only** | NuBus Power Macintosh | 🔴 TODO |
| System 7.5 / 7.5.1 | 1994 | 68000, 4 MB | NuBus Power Macintosh | 🔴 TODO |
| System 7.5.2 | 1995 | PCI Power Macintosh, PowerBook 5300 | (first PCI release) | 🔴 TODO |
| System 7.5.3 / 7.5.5 | 1995-96 | 68000, 4 MB | All in scope | 🔴 TODO |
| Mac OS 7.6 / 7.6.1 | 1997 | **68030+**, 8 MB | All in scope; drops 68000/68020 | 🔴 TODO |
| Mac OS 8.0 | 1997 | **68040 or PowerPC**, 12 MB | Quadra and later | 🔴 TODO |
| Mac OS 8.1 | 1998 | 68040 or PowerPC | **Last release supporting 68k** | 🔴 TODO |
| Mac OS 8.5 / 8.6 | 1998-99 | **PowerPC only** | NuBus Power Macintosh still supported | 🔴 TODO |
| Mac OS 9.0 / 9.0.4 | 1999-2000 | PowerPC, 32 MB | NuBus Power Macintosh (601) still supported | 🔴 TODO |
| Mac OS 9.1 | 2001 | PowerPC, 40 MB | Practical ceiling for a 601 Power Macintosh | 🔴 TODO |
| Mac OS 9.2 – 9.2.2 | 2001 | **PowerPC G3 or later** | Out of scope — no in-scope machine qualifies | ⛔ N/A |

**Where CuteMac stands.** System 6.0.8 is the validated target: both System Tools
and Utilities 1 raw 800K images boot the Macintosh IIcx to Finder from an
unmodified ROM. Earlier System 6 point releases share that path and are expected
to work but are not routinely exercised. System 7.1 is partially reached — the
IIcx path handles its unclean-shutdown warning and Sound Manager behaviour, which
is why the ASC had to be modelled properly — but it is not a validated boot.
Nothing PowerPC-hosted boots yet, so every 7.1.2-and-later PowerPC-only release
is blocked behind the 8100's missing input, floppy, and video-card work.

Practical ceilings worth remembering when scoping: **Mac OS 8.1** is the last
release for any 68k machine, and **Mac OS 9.1** is the last that a NuBus Power
Macintosh can run.

### 6.2 A/UX (Apple Unix, 68k)

A/UX is System V Release 2 with BSD extensions and a Macintosh Finder
environment. It has hard hardware requirements that make it a genuine emulator
stress test: a **PMMU** and, for most releases, an **FPU**.

| Release | Year | Requires | Supported machines | Status |
| --- | --- | --- | --- | --- |
| A/UX 1.0 / 1.1 | 1988-89 | 68020 + 68851 PMMU + 68881 FPU | Macintosh II | 🔴 TODO |
| A/UX 2.0 / 2.0.1 | 1990 | 68020+PMMU or 68030, FPU | II, IIx, IIcx, IIci, SE/30 | 🔴 TODO |
| A/UX 3.0 / 3.0.1 | 1992 | 68030 or 68040, FPU | II family, IIfx, IIsi, Quadra 700/900 | 🔴 TODO |
| A/UX 3.1 | 1993 | 68030 or 68040, FPU | adds Quadra 610/650/800, Centris | 🔴 TODO |
| A/UX 3.1.1 | 1995 | 68030 or 68040, FPU | Final release; adds Workgroup Server 95 | 🔴 TODO |

**Blockers.** A/UX was never ported to PowerPC and never supported the AV
Quadras or the LC line, so its target set is exactly the NuBus 68030/68040
machines. Running it needs three things CuteMac does not yet have together:
a validated **68030 PMMU** translation path under a real OS load (the PMMU is
compiled in and has cache tests, but the IIcx deliberately does not advertise
32-bit mode yet), a validated **68881/68882 FPU** (Musashi's FPU and softfloat
are compiled into the tree but are not independently exercised), and enough SCSI
throughput and correctness to host a Unix root filesystem.

Of the in-scope machines, the **Macintosh IIcx** is the closest to an A/UX
attempt — it is a supported A/UX 2.0 machine and it is CuteMac's most complete
NuBus target.

### 6.3 Server And Workgroup Software

| Item | Runs on | Status |
| --- | --- | --- |
| AppleShare File Server 2.x / 3.x | Any Mac II class or later | 🔴 TODO |
| AppleShare 4.x | Quadra, Workgroup Servers, Power Macintosh | 🔴 TODO |
| Apple Workgroup Server Software (60/80/95) | AWS 60, 80, 95 | 🔴 TODO |
| A/UX 3.1.1 + AppleShare Pro | Apple Workgroup Server 95 | 🔴 TODO |
| At Ease / At Ease for Workgroups | System 7 machines | 🔴 TODO |

### 6.4 AIX

A scope correction worth recording, because it changes what would need to be
built. **IBM AIX for the Apple Network Server (4.1.4 / 4.1.5, 1996) ran on the
Apple Network Server 500 and 700 — PowerPC 604 machines with PCI**, not on the
PowerPC Workgroup Servers.

The **Apple Workgroup Server 6150 / 8150 / 9150** listed in [§1.7](#17-power-macintosh--nubus--pre-pci)
are 601-based NuBus machines that ran **Mac OS plus AppleShare**, not AIX.

| Item | Runs on | In pre-PCI scope? | Status |
| --- | --- | --- | --- |
| IBM AIX 4.1.4 / 4.1.5 for Apple Network Server | Apple Network Server 500 / 700 (PowerPC 604, **PCI**) | ❌ No | ⛔ Out of scope |
| AIX on Workgroup Server 6150/8150/9150 | — | — | ⛔ Does not exist — these ran Mac OS |

If AIX is genuinely wanted as a target, it implies extending scope past the PCI
boundary to the Apple Network Server, which is a different machine architecture
(604, PCI, no ADB console, IBM firmware conventions) and a substantially separate
project from everything else in this document. It is listed here for
completeness rather than as a plan.

### 6.5 Other Operating Systems (Non-Apple, Popular)

Not Apple system software, but frequently run on these machines and useful as
independent correctness tests because they exercise the MMU and interrupt paths
much harder than Mac OS does.

| Item | Runs on | Status |
| --- | --- | --- |
| MkLinux DR1+ | **Power Macintosh 6100/7100/8100** — the original target | 🔴 TODO |
| NetBSD/mac68k | 68020+PMMU through Quadra | 🔴 TODO |
| Linux/m68k | 68030/68040 Macs | 🔴 TODO |
| MachTen (Tenon) | System 7 machines, hosted Unix | 🔴 TODO |
| NetBSD/macppc (pre-PCI subset) | NuBus Power Macintosh | 🔴 TODO |

MkLinux is worth singling out: its first release targeted precisely the NuBus
Power Macintosh trio that CuteMac's 8100 work is aimed at.

---

## 7. Cross-Reference: What Exists In The Tree Today

For orientation when reading the tables above, this is the actual inventory.

**CPU cores.** `M68kCpuCore` (Musashi-derived, models 68000/68010/68EC020/68020/
68EC030/68030/68EC040/68LC040/68040, PMMU and FPU compiled in, optional external
68851). `PowerPc601Core` (portable interpreter, BAT/page translation, exceptions,
trace rings).

**Machines.** `MacPlusMachine` (+128K/512K/512Ke variants), `MacIIcxMachine`,
`Quadra700Machine`, `PowerMac8100Machine`.

**Devices.** `Via6522`, `Z8530Scc`, `MacRtc`, `IwmController` (IWM + SWIM1),
`FloppyDiskImage`, `Ncr5380` + `MacintoshNcr5380Bus`, `Ncr53c94`, `ScsiBus`,
`ScsiBlockDevice`, `ScsiCdRomDevice`, `AdbBus` + `AdbTransceiver`,
`CudaController`, `AppleSoundChip`, `NuBusBus`, `MacintoshIIVideoCard`,
`AppleDisplayCard`, `CuteMacVideoCard`, `CuteMacAcceleratedVideoCard`,
`AppleNuBusEthernetCard`, `DafbVideo`, `SonoraVideo`, `ImageWriterII`,
`HayesModem`, `NullModem`, `SlirpEthernetBackend`.

**Not present at all.** Egret, Power Manager, IOP, OSS, RBV/VDAC, V8, Valkyrie,
Civic, DSP3210, Singer/AWACS, SONIC, MACE, SWIM2/SWIM3, IDE, LocalTalk framing,
HPV/AV PDS video, any PDS bus, any portable chassis.

---

## 8. Suggested Order Of Attack

Not a commitment — a reading of the tables. Each step reuses more of what exists
than the one after it.

1. **Finish the Quadra 700 to a Finder boot.** DAFB and the 68040 map exist; the
   gap is system-software-level validation and SWIM/SCSI integration.
2. **Add SONIC Ethernet.** Unblocks period-correct networking on Quadra 700/800
   and every Power Macintosh, and the `PacketNetworkBackend` boundary is already
   proven by the NuBus card.
3. **Give the 8100 input, floppy, and a real video card.** `queueInput` is empty
   and `loadFloppyImage` returns `false`; both block every PowerPC OS test.
4. **Implement the Macintosh SE/30.** Highest reuse-to-effort ratio on the 68k
   side — a IIcx without NuBus, and the natural A/UX 2.0 candidate.
5. **Implement Egret.** One controller unlocks the IIsi, IIvx, and the entire LC
   family, and shares its packet layer with the existing Cuda work.
6. **Validate PMMU and FPU under load**, which is the real precondition for A/UX
   on any machine.
