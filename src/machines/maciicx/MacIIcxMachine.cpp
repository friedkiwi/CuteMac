#include "cutemac/machines/maciicx/MacIIcxMachine.h"
#include "cutemac/devices/video/nubus/CuteMacAcceleratedVideoCard.h"
#include "cutemac/devices/video/nubus/CuteMacVideoCard.h"
#include "cutemac/rom/RomPatcher.h"

#include <QFile>

#include <algorithm>

namespace cutemac::machines::maciicx {

namespace {

constexpr std::uint32_t romBase = 0x40000000;
constexpr std::uint32_t ioBase = 0x50000000;
constexpr std::uint32_t ioMirrorMask = 0x00f00000;
constexpr std::uint32_t ioOffsetMask = 0x000fffff;
constexpr int cpuToViaRatio = 20;
constexpr int viaVblPeriod = 13030;
constexpr std::uint32_t ramMapSearchEnd = 192U * 1024U * 1024U;

std::uint8_t highByte(std::uint16_t value) { return static_cast<std::uint8_t>(value >> 8); }
std::uint8_t lowByte(std::uint16_t value) { return static_cast<std::uint8_t>(value); }
bool isScsiDma(std::uint32_t address)
{
    if ((address & ~ioMirrorMask & 0xfff00000U) != ioBase) return false;
    const auto offset = address & ioOffsetMask;
    return (offset >= 0x6000 && offset < 0x8000) || (offset >= 0x12000 && offset < 0x14000);
}

} // namespace

MacIIcxMachine::MacIIcxMachine(std::size_t ramSize, const QString& nvramPath)
    : m_ram(static_cast<qsizetype>(std::max<std::size_t>(ramSize, 1024 * 1024)), 0)
    , m_scsiBus(m_scsi, {
          devices::scsi::ncr5380::MacintoshNcr5380Bus::RegisterLane::MostSignificant,
          devices::scsi::ncr5380::MacintoshNcr5380Bus::RegisterLane::MostSignificant,
          true,
          true,
      })
{
    (void)m_rtc.setNvramImagePath(nvramPath);
    m_cpu.setModel(cpu::m68k::M68kCpuCore::Model::M68030);
    m_cpu.setBus(this);

    m_via1.setPowerOnState(0, 0, 0, 0);
    m_via2.setPowerOnState(0, 0, 0, 0);
    m_via1.setAutomaticCa1Period(viaVblPeriod);
    m_via2.setAutomaticCa1Period(0);
    m_via1.setPortAChangedCallback([this](std::uint8_t value) {
        const auto overlay = (value & 0x10) != 0;
        if (m_overlay != overlay) {
            m_overlay = overlay;
            rebuildPhysicalMemoryMap();
        }
        m_swim.setSideSelect((value & 0x20) != 0);
    });
    m_via1.setPortBChangedCallback([this](std::uint8_t value, std::uint8_t ddr) {
        m_adbTransceiver.setViaState(static_cast<std::uint8_t>((value >> 4) & 3));
        m_rtc.setPins((value & 0x04) == 0, (value & 0x02) != 0, (value & 0x01) != 0 && (ddr & 0x01) != 0);
        updateViaInputs();
    });
    m_via1.setShiftRegisterWriteCallback([this](std::uint8_t value) { m_adbTransceiver.shiftRegisterWritten(value); });
    m_adbTransceiver.setReceiveByteCallback([this](std::uint8_t value) { m_via1.externalShiftIn(value); });
    m_adbTransceiver.setTransmitCompleteCallback([this]() { m_via1.externalShiftOutComplete(); });
    m_adbTransceiver.setTraceContextCallback([this]() {
        const auto via = m_via1.debugState();
        return devices::adb::AdbTransceiver::TraceContext {
            m_cpu.programCounter(), via.auxiliaryControl, via.shiftRegister,
            via.interruptFlags, via.interruptEnable, via.portB,
        };
    });
    m_adbTransceiver.setIrqCallback([this](bool asserted) {
        m_adbIrqPending = asserted;
        m_via1.setPortBInputBit(3, !m_adbIrqPending);
        updateInterrupts();
    });
    m_via2.setPortAChangedCallback([this](std::uint8_t value) {
        const auto glueRamSize = static_cast<std::uint8_t>(value & 0xc0);
        if (m_glueRamSize != glueRamSize) {
            m_glueRamSize = glueRamSize;
            rebuildPhysicalMemoryMap();
        }
        updateViaInputs();
    });
    m_nubus.setSlotIrqCallback([this](int slot, bool asserted) {
        const auto mask = static_cast<std::uint8_t>(1U << (slot - 9));
        if (asserted) m_nubusIrqState &= static_cast<std::uint8_t>(~mask);
        else m_nubusIrqState |= mask;
        updateViaInputs();
        if (asserted) {
            // Match MAME's missed-ack handling: every slot reassertion must
            // create a new falling CA1 edge even if the line was already low.
            m_via2.setCa1(true);
            m_via2.setCa1(false);
        } else {
            m_via2.setCa1(true);
        }
        updateInterrupts();
    });
    updateViaInputs();
}

QString MacIIcxMachine::machineId() const { return QStringLiteral("mac-iicx"); }

void MacIIcxMachine::attachSerialEndpoint(int channel, std::shared_ptr<devices::serial::SerialEndpoint> endpoint)
{
    m_scc.attachEndpoint(channel == 0 ? devices::scc::Z8530Scc::Channel::A : devices::scc::Z8530Scc::Channel::B, std::move(endpoint));
}

bool MacIIcxMachine::loadRomFile(const QString& path, const QStringList& patches)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return false;
    auto bytes = file.readAll();
    if (bytes.size() != static_cast<qsizetype>(m_rom.size())) return false;
    const auto patchResult = rom::RomPatcher::apply(bytes, machineId(), patches);
    if (!patchResult.success) return false;
    std::copy(bytes.cbegin(), bytes.cend(), m_rom.begin());
    m_romPath = path;
    m_romLoaded = true;
    return true;
}

bool MacIIcxMachine::loadDiskImage(const QString& path) { return loadScsiDisk(0, path, false); }
void MacIIcxMachine::ejectDiskImage() { ejectScsiDevice(0); }

bool MacIIcxMachine::loadScsiDisk(int id, const QString& path, bool readOnly)
{
    if (id < 0 || id >= static_cast<int>(m_scsiDisks.size())) return false;
    auto disk = std::make_shared<devices::scsi::ScsiBlockDevice>();
    if (!disk->loadImage(path, readOnly)) return false;
    m_scsiCdRoms[static_cast<std::size_t>(id)].reset();
    m_scsiDisks[static_cast<std::size_t>(id)] = disk;
    m_scsi.attachTarget(static_cast<std::uint8_t>(id), disk);
    return true;
}

bool MacIIcxMachine::loadScsiCdRom(int id, const QString& path)
{
    if (id < 0 || id >= static_cast<int>(m_scsiCdRoms.size())) return false;
    auto cdRom = m_scsiCdRoms[static_cast<std::size_t>(id)];
    if (!cdRom) cdRom = std::make_shared<devices::scsi::ScsiCdRomDevice>();
    if (!path.isEmpty() && !cdRom->loadImage(path)) return false;
    m_scsiDisks[static_cast<std::size_t>(id)].reset();
    m_scsiCdRoms[static_cast<std::size_t>(id)] = cdRom;
    m_scsi.attachTarget(static_cast<std::uint8_t>(id), cdRom);
    return true;
}

void MacIIcxMachine::ejectScsiCdRom(int id)
{
    if (id < 0 || id >= static_cast<int>(m_scsiCdRoms.size())) return;
    const auto& cdRom = m_scsiCdRoms[static_cast<std::size_t>(id)];
    if (cdRom) cdRom->eject();
}

void MacIIcxMachine::ejectScsiDevice(int id)
{
    if (id < 0 || id >= static_cast<int>(m_scsiDisks.size())) return;
    m_scsi.detachTarget(static_cast<std::uint8_t>(id));
    m_scsiDisks[static_cast<std::size_t>(id)].reset();
    m_scsiCdRoms[static_cast<std::size_t>(id)].reset();
}

bool MacIIcxMachine::loadFloppyImage(const QString& path, bool readOnly)
{
    return loadFloppyImage(0, path, readOnly);
}

bool MacIIcxMachine::loadFloppyImage(int drive, const QString& path, bool readOnly)
{
    if (drive < 0 || drive >= static_cast<int>(m_floppyPaths.size())) return false;
    if (!m_swim.loadFloppyImage(drive, path, readOnly)) return false;
    m_floppyPaths[static_cast<std::size_t>(drive)] = path;
    return true;
}

void MacIIcxMachine::ejectFloppyImage()
{
    ejectFloppyImage(0);
}

void MacIIcxMachine::ejectFloppyImage(int drive)
{
    if (drive < 0 || drive >= static_cast<int>(m_floppyPaths.size())) return;
    m_swim.ejectFloppyImage(drive);
    m_floppyPaths[static_cast<std::size_t>(drive)].clear();
}

void MacIIcxMachine::reset()
{
    std::fill(m_ram.begin(), m_ram.end(), 0);
    m_nubusIrqState = 0x3f;
    m_adbIrqPending = false;
    m_hostMousePositionValid = false;
    m_glueRamSize = 0;
    m_viaCycleRemainder = 0;
    m_ioStatistics = {};
    m_scheduler.reset();
    m_scc.reset();
    m_swim.reset();
    m_asc.reset();
    m_adbTransceiver.reset();
    m_scsi.reset();
    for (std::size_t id = 0; id < m_scsiDisks.size(); ++id) {
        if (m_scsiDisks[id]) m_scsi.attachTarget(static_cast<std::uint8_t>(id), m_scsiDisks[id]);
        else if (m_scsiCdRoms[id]) {
            m_scsiCdRoms[id]->acknowledgeMediaChange();
            m_scsi.attachTarget(static_cast<std::uint8_t>(id), m_scsiCdRoms[id]);
        }
    }
    m_via1.reset();
    m_via2.reset();
    m_nubus.reset();
    m_overlay = true;
    rebuildPhysicalMemoryMap();
    updateViaInputs();
    m_cpu.reset();
    updateInterrupts();
}

bool MacIIcxMachine::triggerProgrammersInterrupt()
{
    // Physical programmer/debug switches on 68k Macs assert an NMI-style
    // level-7 edge.  Normal GLUE/VIA/SCC levels are recomputed before and
    // after subsequent instructions.
    m_cpu.setIrqLevel(0);
    m_cpu.setIrqLevel(7);
    return true;
}

int MacIIcxMachine::runCycles(int cycles)
{
    int used = 0;
    while (used < cycles) {
        used += stepInstruction();
    }
    return used;
}

int MacIIcxMachine::stepInstruction()
{
    m_scheduler.dispatchDue();
    updateInterrupts();
    const auto instructionCycles = std::max(1, m_cpu.stepInstruction());
    advanceDevices(instructionCycles);
    m_scheduler.advance(static_cast<std::uint64_t>(instructionCycles));
    return instructionCycles;
}

std::uint64_t MacIIcxMachine::cycleCount() const { return m_scheduler.now(); }
std::uint32_t MacIIcxMachine::programCounter() const { return m_cpu.programCounter(); }
std::uint64_t MacIIcxMachine::diskActivityCounter() const
{
    std::uint64_t activity = 0;
    for (const auto& disk : m_scsiDisks) {
        if (disk) activity += disk->activityCounter();
    }
    for (const auto& cdRom : m_scsiCdRoms) {
        if (cdRom) activity += cdRom->activityCounter();
    }
    activity += m_swim.activityCounter();
    return activity;
}
bool MacIIcxMachine::overlayEnabled() const { return m_overlay; }

QByteArray MacIIcxMachine::framebufferBytes() const
{
    return videoFrame().pixels;
}

devices::video::VideoFrame MacIIcxMachine::videoFrame() const
{
    for (int slot = 9; slot <= 11; ++slot) {
        const auto card = m_nubus.card(slot);
        if (!card) continue;
        const auto frame = card->videoFrame();
        if (frame.valid()) return frame;
    }
    return {};
}

core::GuestPowerRequest MacIIcxMachine::takePowerRequest()
{
    for (int slot = 9; slot <= 14; ++slot) {
        const auto card = m_nubus.card(slot);
        if (!card) continue;
        const auto request = card->takePowerRequest();
        if (request != core::GuestPowerRequest::None) return request;
    }
    return core::GuestPowerRequest::None;
}

void MacIIcxMachine::queueInput(const core::GuestInputEvent& event, std::uint64_t cycle)
{
    m_scheduler.schedule(std::max(cycle, m_scheduler.now()), [this, event]() { applyInput(event); });
}

std::uint8_t MacIIcxMachine::read8(std::uint32_t address)
{
    std::uint8_t directValue = 0;
    if (m_physicalMemoryMap.tryRead8(address, directValue)) return directValue;
    if (m_overlay && address < m_rom.size()) return m_rom[address];
    if (const auto index = ramIndex(address)) return m_ram[static_cast<qsizetype>(*index)];
    if ((address & 0xf0000000U) == romBase) return m_rom[(address - romBase) & (m_rom.size() - 1)];
    if (isIo(address)) return readIo8(address);
    if (devices::nubus::NuBusBus::standardSlot(address) >= 0) {
        ++m_ioStatistics.nubusReads;
        return m_nubus.read8(address);
    }
    return 0xff;
}

std::uint16_t MacIIcxMachine::read16(std::uint32_t address)
{
    std::uint16_t directValue = 0;
    if (m_physicalMemoryMap.tryRead16(address, directValue)) return directValue;
    if (devices::nubus::NuBusBus::standardSlot(address) >= 0) {
        ++m_ioStatistics.nubusReads;
        return m_nubus.read16(address);
    }
    if (isScsiDma(address)) {
        m_ioStatistics.scsiReads += 2;
        return static_cast<std::uint16_t>(m_scsiBus.readPseudoDma(2));
    }
    if (isIo(address)) {
        const auto value = readIo8(address);
        return static_cast<std::uint16_t>((value << 8) | value);
    }
    return static_cast<std::uint16_t>((read8(address) << 8) | read8(address + 1));
}

std::uint32_t MacIIcxMachine::read32(std::uint32_t address)
{
    std::uint32_t directValue = 0;
    if (m_physicalMemoryMap.tryRead32(address, directValue)) return directValue;
    if (devices::nubus::NuBusBus::standardSlot(address) >= 0) {
        ++m_ioStatistics.nubusReads;
        return m_nubus.read32(address);
    }
    return (static_cast<std::uint32_t>(read16(address)) << 16) | read16(address + 2);
}

void MacIIcxMachine::write8(std::uint32_t address, std::uint8_t value)
{
    if (m_physicalMemoryMap.tryWrite8(address, value)) return;
    if (const auto index = ramIndex(address); index && !(m_overlay && address < m_rom.size())) {
        m_ram[static_cast<qsizetype>(*index)] = value;
    } else if (isIo(address)) {
        writeIo8(address, value);
    } else if (devices::nubus::NuBusBus::standardSlot(address) >= 0) {
        ++m_ioStatistics.nubusWrites;
        m_nubus.write8(address, value);
    }
}

std::optional<std::size_t> MacIIcxMachine::ramIndex(std::uint32_t address) const
{
    const auto memorySize = static_cast<std::size_t>(m_ram.size());
    std::size_t bankASize = std::min<std::size_t>(memorySize, 64U * 1024U * 1024U);
    std::size_t bankBSize = memorySize > bankASize ? memorySize - bankASize : 0;
    bool noMirror = false;
    bool mirrorBankB = false;

    switch (memorySize / (1024U * 1024U)) {
    case 1: case 16:
        noMirror = true;
        break;
    case 2:
        bankASize = bankBSize = 1U * 1024U * 1024U;
        break;
    case 4:
        noMirror = true;
        bankASize = 4U * 1024U * 1024U;
        bankBSize = 0;
        break;
    case 5:
        bankASize = 4U * 1024U * 1024U;
        bankBSize = 1U * 1024U * 1024U;
        mirrorBankB = true;
        break;
    case 8:
        bankASize = bankBSize = 4U * 1024U * 1024U;
        break;
    case 17:
        bankASize = 16U * 1024U * 1024U;
        bankBSize = 1U * 1024U * 1024U;
        mirrorBankB = true;
        break;
    case 20:
        bankASize = 16U * 1024U * 1024U;
        bankBSize = 4U * 1024U * 1024U;
        mirrorBankB = true;
        break;
    case 65:
        bankASize = 64U * 1024U * 1024U;
        bankBSize = 1U * 1024U * 1024U;
        mirrorBankB = true;
        break;
    case 68:
        bankASize = 64U * 1024U * 1024U;
        bankBSize = 4U * 1024U * 1024U;
        mirrorBankB = true;
        break;
    case 80:
        bankASize = 64U * 1024U * 1024U;
        bankBSize = 16U * 1024U * 1024U;
        mirrorBankB = true;
        break;
    case 128:
        bankASize = bankBSize = 64U * 1024U * 1024U;
        break;
    default:
        break;
    }

    // GLUE moves bank B while the ROM probes the installed SIMM depth:
    // PA7:PA6 00/01/10/11 select 1/2/8/32 MiB respectively.
    constexpr std::array<std::size_t, 4> bankBLocations {
        1U * 1024U * 1024U,
        2U * 1024U * 1024U,
        8U * 1024U * 1024U,
        32U * 1024U * 1024U,
    };
    const auto bankBLocation = bankBLocations[m_glueRamSize >> 6];

    // The FDHD/IIx/IIcx ROM requires some RAM before it starts sizing, but
    // exposing the complete allocation in an oversized GLUE window makes it
    // mis-detect the SIMMs.  Match the hardware-visible probe window used by
    // MAME: retain only the first MiB until the selected window fits.
    // A single fully populated bank A (1/4/16 MiB configurations) remains
    // contiguous regardless of the bank-B sizing selector.  MODE32 enters
    // 32-bit startup while VIA2 still selects the 32 MiB probe window and
    // immediately uses RAM above 1 MiB for its supervisor stack.
    if (!noMirror && bankBLocation > memorySize)
        return address < std::min<std::size_t>(memorySize, 1U * 1024U * 1024U)
            ? std::optional<std::size_t>(address)
            : std::nullopt;

    if (address < memorySize) return address;
    if (!noMirror && address < memorySize + (mirrorBankB ? bankBSize : bankASize)) {
        return (mirrorBankB ? bankASize : 0) + (address - memorySize);
    }
    if (bankBSize > 0 && bankBLocation >= memorySize + bankASize
        && address >= bankBLocation && address < bankBLocation + bankBSize) {
        return bankASize + (address - bankBLocation);
    }
    return std::nullopt;
}

void MacIIcxMachine::write16(std::uint32_t address, std::uint16_t value)
{
    if (m_physicalMemoryMap.tryWrite16(address, value)) return;
    if (isScsiDma(address)) {
        m_ioStatistics.scsiWrites += 2;
        m_scsiBus.writePseudoDma(2, value);
        return;
    }
    if (isIo(address)) {
        writeIo8(address, highByte(value));
        return;
    }
    if (devices::nubus::NuBusBus::standardSlot(address) >= 0) {
        ++m_ioStatistics.nubusWrites;
        m_nubus.write16(address, value);
        return;
    }
    write8(address, highByte(value));
    write8(address + 1, lowByte(value));
}

void MacIIcxMachine::write32(std::uint32_t address, std::uint32_t value)
{
    if (m_physicalMemoryMap.tryWrite32(address, value)) return;
    if (devices::nubus::NuBusBus::standardSlot(address) >= 0) {
        ++m_ioStatistics.nubusWrites;
        m_nubus.write32(address, value);
        return;
    }
    write16(address, static_cast<std::uint16_t>(value >> 16));
    write16(address + 2, static_cast<std::uint16_t>(value));
}

void MacIIcxMachine::rebuildPhysicalMemoryMap()
{
    m_physicalMemoryMap.clear();
    for (std::uint32_t address = 0; address < ramMapSearchEnd;
         address += core::PhysicalMemoryMap::pageSize) {
        const auto index = ramIndex(address);
        if (index && *index + core::PhysicalMemoryMap::pageSize <= static_cast<std::size_t>(m_ram.size())) {
            m_physicalMemoryMap.mapReadWritePage(address, m_ram.data() + *index);
        }
    }

    m_physicalMemoryMap.mapReadOnlyMirrored(romBase, 0x10000000U,
        m_rom.data(), static_cast<std::uint32_t>(m_rom.size()));
    if (m_overlay) {
        for (std::uint32_t address = 0; address < m_rom.size(); address += core::PhysicalMemoryMap::pageSize) {
            m_physicalMemoryMap.mapReadOnlyPage(address, m_rom.data() + address);
        }
    }
}

bool MacIIcxMachine::installNuBusCard(int slot, std::shared_ptr<devices::nubus::NuBusCard> card)
{
    return slot >= 9 && slot <= 11 && m_nubus.install(slot, std::move(card));
}

cpu::m68k::M68kCpuCore::RegisterSnapshot MacIIcxMachine::cpuRegisters() const { return m_cpu.registers(); }
QString MacIIcxMachine::disassemble(std::uint32_t address) const { return m_cpu.disassemble(address); }
int MacIIcxMachine::disassembleBytes(std::uint32_t address) const { return m_cpu.disassembleBytes(address); }

QString MacIIcxMachine::debugCpuArchitecture() const { return QStringLiteral("m68k:68030"); }

QStringList MacIIcxMachine::debugRegisterLines() const
{
    const auto regs = cpuRegisters();
    QStringList lines;
    for (int base = 0; base < 8; base += 4) {
        lines.append(QStringLiteral("D%1=%2 D%3=%4 D%5=%6 D%7=%8")
                         .arg(base).arg(regs.d[base], 8, 16, QLatin1Char('0'))
                         .arg(base + 1).arg(regs.d[base + 1], 8, 16, QLatin1Char('0'))
                         .arg(base + 2).arg(regs.d[base + 2], 8, 16, QLatin1Char('0'))
                         .arg(base + 3).arg(regs.d[base + 3], 8, 16, QLatin1Char('0')));
        lines.append(QStringLiteral("A%1=%2 A%3=%4 A%5=%6 A%7=%8")
                         .arg(base).arg(regs.a[base], 8, 16, QLatin1Char('0'))
                         .arg(base + 1).arg(regs.a[base + 1], 8, 16, QLatin1Char('0'))
                         .arg(base + 2).arg(regs.a[base + 2], 8, 16, QLatin1Char('0'))
                         .arg(base + 3).arg(regs.a[base + 3], 8, 16, QLatin1Char('0')));
    }
    lines.append(QStringLiteral("PC=%1 SR=%2 USP=%3 ISP=%4 MSP=%5 VBR=%6")
                     .arg(regs.pc, 8, 16, QLatin1Char('0')).arg(regs.sr, 4, 16, QLatin1Char('0'))
                     .arg(regs.usp, 8, 16, QLatin1Char('0')).arg(regs.isp, 8, 16, QLatin1Char('0'))
                     .arg(regs.msp, 8, 16, QLatin1Char('0')).arg(regs.vbr, 8, 16, QLatin1Char('0')));
    lines.append(QStringLiteral("PMMU=%1 KIND=%2 TC=%3 TT0=%4 TT1=%5 MMUSR=%6 PHYS_PC=%7")
                     .arg(regs.pmmuEnabled ? QStringLiteral("on") : QStringLiteral("off"))
                     .arg(regs.pmmuKind)
                     .arg(regs.pmmuTc, 8, 16, QLatin1Char('0'))
                     .arg(regs.pmmuTt0, 8, 16, QLatin1Char('0'))
                     .arg(regs.pmmuTt1, 8, 16, QLatin1Char('0'))
                     .arg(regs.pmmuMmusr, 4, 16, QLatin1Char('0'))
                     .arg(regs.physicalPc, 8, 16, QLatin1Char('0')));
    lines.append(QStringLiteral("CRP=%1:%2 SRP=%3:%4")
                     .arg(regs.pmmuCrpLimit, 8, 16, QLatin1Char('0'))
                     .arg(regs.pmmuCrpAddress, 8, 16, QLatin1Char('0'))
                     .arg(regs.pmmuSrpLimit, 8, 16, QLatin1Char('0'))
                     .arg(regs.pmmuSrpAddress, 8, 16, QLatin1Char('0')));
    lines.append(QStringLiteral("ATC_HITS=%1 ATC_MISSES=%2 LAST_MMU_FAULT=%3")
                     .arg(regs.pmmuAtcHits)
                     .arg(regs.pmmuAtcMisses)
                     .arg(regs.pmmuFaultAddress, 8, 16, QLatin1Char('0')));
    return lines;
}

std::uint8_t MacIIcxMachine::debugRead8(std::uint32_t address) const { return const_cast<MacIIcxMachine*>(this)->read8(address); }
std::uint16_t MacIIcxMachine::debugRead16(std::uint32_t address) const { return const_cast<MacIIcxMachine*>(this)->read16(address); }
std::uint32_t MacIIcxMachine::debugRead32(std::uint32_t address) const { return const_cast<MacIIcxMachine*>(this)->read32(address); }
void MacIIcxMachine::debugWrite8(std::uint32_t address, std::uint8_t value) { write8(address, value); }
void MacIIcxMachine::debugWrite16(std::uint32_t address, std::uint16_t value) { write16(address, value); }
void MacIIcxMachine::debugWrite32(std::uint32_t address, std::uint32_t value) { write32(address, value); }

bool MacIIcxMachine::isIo(std::uint32_t address) const
{
    return (address & ~ioMirrorMask & 0xfff00000U) == ioBase;
}

std::uint8_t MacIIcxMachine::readIo8(std::uint32_t address)
{
    const auto offset = address & ioOffsetMask;
    if (offset < 0x2000 || (offset >= 0x40000 && offset < 0x42000)) {
        return m_via1.readRegister(static_cast<std::uint8_t>((offset >> 9) & 0x0f));
    }
    if (offset >= 0x2000 && offset < 0x4000) return m_via2.readRegister(static_cast<std::uint8_t>((offset >> 9) & 0x0f));
    if (offset >= 0x4000 && offset < 0x6000) {
        using Channel = devices::scc::Z8530Scc::Channel;
        switch ((offset - 0x4000) & 6) {
        case 6: return m_scc.readData(Channel::A);
        case 4: return m_scc.readData(Channel::B);
        case 2: return m_scc.readControl(Channel::A);
        default: return m_scc.readControl(Channel::B);
        }
    }
    if ((offset >= 0x6000 && offset < 0x8000) || (offset >= 0x12000 && offset < 0x14000)) {
        ++m_ioStatistics.scsiReads;
        // Macintosh blind pseudo-DMA relies on GLUE holding off DSACK until
        // the 5380 presents DRQ.  Our CPU bus cannot wait, so observe the
        // status line here before completing each aperture access.  Keep this
        // mediation out of the NCR5380 itself: a DACK without REQ must remain
        // a non-transfer for checked/restarted System 7 accesses.
        return static_cast<std::uint8_t>(m_scsiBus.readPseudoDma(1));
    }
    if (offset >= 0x10000 && offset < 0x12000) {
        ++m_ioStatistics.scsiReads;
        const auto reg = static_cast<std::uint8_t>(((offset - 0x10000) >> 4) & 7);
        // MAME's 0x130 handler offset is expressed in 16-bit words.  On the
        // 68030 byte-addressed bus the register-6 DACK alias is therefore at
        // $50010260, not $50010130.  Keep the comparison exact: A/UX reads
        // register 7 at $50010070 during selection and must not acknowledge
        // the target's first command-phase REQ.
        const auto dack = offset == 0x10260;
        return static_cast<std::uint8_t>(dack ? m_scsiBus.readPseudoDma(1) : m_scsiBus.readRegister(reg, 1));
    }
    if (offset >= 0x14000 && offset < 0x16000) return m_asc.read(static_cast<std::uint16_t>(offset & 0x0fff));
    if (offset >= 0x16000 && offset < 0x18000) {
        ++m_ioStatistics.swimReads;
        return m_swim.access(static_cast<std::uint8_t>(((offset - 0x16000) >> 9) & 0x0f));
    }
    return 0xff;
}

void MacIIcxMachine::writeIo8(std::uint32_t address, std::uint8_t value)
{
    const auto offset = address & ioOffsetMask;
    if (offset < 0x2000 || (offset >= 0x40000 && offset < 0x42000)) {
        m_via1.writeRegister(static_cast<std::uint8_t>((offset >> 9) & 0x0f), value);
    } else if (offset >= 0x2000 && offset < 0x4000) {
        m_via2.writeRegister(static_cast<std::uint8_t>((offset >> 9) & 0x0f), value);
    } else if (offset >= 0x4000 && offset < 0x6000) {
        using Channel = devices::scc::Z8530Scc::Channel;
        switch ((offset - 0x4000) & 6) {
        case 6: m_scc.writeData(Channel::A, value); break;
        case 4: m_scc.writeData(Channel::B, value); break;
        case 2: m_scc.writeControl(Channel::A, value); break;
        default: m_scc.writeControl(Channel::B, value); break;
        }
    } else if ((offset >= 0x6000 && offset < 0x8000) || (offset >= 0x12000 && offset < 0x14000)) {
        ++m_ioStatistics.scsiWrites;
        m_scsiBus.writePseudoDma(1, value);
    } else if (offset >= 0x10000 && offset < 0x12000) {
        ++m_ioStatistics.scsiWrites;
        const auto reg = static_cast<std::uint8_t>(((offset - 0x10000) >> 4) & 7);
        // The Macintosh GLUE aliases are direction-dependent: pseudo-DMA
        // reads DACK NCR register 6 at byte offset $260, while writes DACK
        // register 0 at byte offset $200.  MAME's corresponding $130/$100
        // offsets are 16-bit word offsets, not physical byte addresses.
        const auto dack = offset == 0x10200;
        if (dack) m_scsiBus.writePseudoDma(1, value);
        else m_scsiBus.writeRegister(reg, 1, value);
    } else if (offset >= 0x14000 && offset < 0x16000) {
        m_asc.write(static_cast<std::uint16_t>(offset & 0x0fff), value);
    } else if (offset >= 0x16000 && offset < 0x18000) {
        ++m_ioStatistics.swimWrites;
        (void)m_swim.access(static_cast<std::uint8_t>(((offset - 0x16000) >> 9) & 0x0f), value, true);
    }
    updateInterrupts();
}

void MacIIcxMachine::applyInput(const core::GuestInputEvent& event)
{
    switch (event.type) {
    case core::GuestInputEvent::Type::MouseDelta:
        m_adbTransceiver.moveMouse(static_cast<std::int16_t>(event.first), static_cast<std::int16_t>(event.second));
        m_hostMousePositionValid = false;
        break;
    case core::GuestInputEvent::Type::MouseButton:
        m_adbTransceiver.setMouseButton(event.pressed);
        break;
    case core::GuestInputEvent::Type::Key:
        m_adbTransceiver.queueKey(static_cast<std::uint8_t>(event.first), event.pressed);
        break;
    case core::GuestInputEvent::Type::ResetKeyboard:
        m_adbTransceiver.resetInput();
        m_hostMousePositionValid = false;
        break;
    case core::GuestInputEvent::Type::MousePosition: {
        const auto x = static_cast<std::int16_t>(event.first);
        const auto y = static_cast<std::int16_t>(event.second);
        std::shared_ptr<devices::video::nubus::CuteMacVideoCard> integratedVideo;
        std::shared_ptr<devices::video::nubus::CuteMacAcceleratedVideoCard> acceleratedVideo;
        for (int slot = 9; slot <= 14 && !integratedVideo && !acceleratedVideo; ++slot) {
            integratedVideo = std::dynamic_pointer_cast<devices::video::nubus::CuteMacVideoCard>(m_nubus.card(slot));
            acceleratedVideo = std::dynamic_pointer_cast<devices::video::nubus::CuteMacAcceleratedVideoCard>(m_nubus.card(slot));
        }
        // Integrated pointing owns host position events for the lifetime of
        // the configured card.  The slot-VBL driver may enable its interrupt
        // late during MODE32 startup, or briefly disable it across a mode
        // change; keep publishing the latest mailbox coordinates throughout
        // those intervals and never leak them into relative ADB movement.
        if (integratedVideo && integratedVideo->absolutePointerEnabled()) {
            integratedVideo->setHostPointerPosition(x, y);
        } else if (acceleratedVideo && acceleratedVideo->absolutePointerEnabled()) {
            acceleratedVideo->setHostPointerPosition(x, y);
        } else if (m_hostMousePositionValid) {
            m_adbTransceiver.moveMouse(static_cast<std::int16_t>(x - m_hostMouseX),
                static_cast<std::int16_t>(y - m_hostMouseY));
        }
        m_hostMouseX = x;
        m_hostMouseY = y;
        m_hostMousePositionValid = true;
        break;
    }
    }
}

void MacIIcxMachine::updateInterrupts()
{
    // Mac II GLUE routes the ASC FIFO IRQ, active high at the chip, through
    // an inverter to VIA2 CB1.
    m_via2.setCb1(!m_asc.interruptActive());
    const auto level = m_scc.interruptActive() ? 4U
        : (m_via2.interruptActive() ? 2U : (m_via1.interruptActive() ? 1U : 0U));
    m_cpu.setIrqLevel(level);
}

void MacIIcxMachine::updateViaInputs()
{
    const std::uint8_t via1A = 0xc1;
    const auto via2A = static_cast<std::uint8_t>(m_glueRamSize | m_nubusIrqState);
    const std::uint8_t via2B = 0xcf;
    for (int bit = 0; bit < 8; ++bit) {
        m_via1.setPortAInputBit(static_cast<std::uint8_t>(bit), (via1A & (1 << bit)) != 0);
        m_via2.setPortAInputBit(static_cast<std::uint8_t>(bit), (via2A & (1 << bit)) != 0);
        m_via2.setPortBInputBit(static_cast<std::uint8_t>(bit), (via2B & (1 << bit)) != 0);
    }
    m_via1.setPortBInputBit(0, m_rtc.dataLine());
    m_via1.setPortBInputBit(3, !m_adbIrqPending);
}

void MacIIcxMachine::advanceDevices(int cpuCycles)
{
    m_scc.tick(cpuCycles);
    m_asc.tick(static_cast<std::uint64_t>(cpuCycles));
    m_adbTransceiver.tick(cpuCycles);
    m_viaCycleRemainder += cpuCycles;
    const auto viaCycles = m_viaCycleRemainder / cpuToViaRatio;
    m_viaCycleRemainder %= cpuToViaRatio;
    if (viaCycles > 0) {
        m_via1.tick(viaCycles);
        m_via2.tick(viaCycles);
    }
    m_nubus.tick(static_cast<std::uint64_t>(cpuCycles));
    updateInterrupts();
}

} // namespace cutemac::machines::maciicx
