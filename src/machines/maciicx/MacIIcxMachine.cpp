#include "cutemac/machines/maciicx/MacIIcxMachine.h"
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
          .registerLane = devices::scsi::ncr5380::MacintoshNcr5380Bus::RegisterLane::MostSignificant,
          .pseudoDmaLane = devices::scsi::ncr5380::MacintoshNcr5380Bus::RegisterLane::MostSignificant,
          .pseudoDmaBurst = true,
          .waitForDrq = true,
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
        m_overlay = (value & 0x10) != 0;
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
    m_adbTransceiver.setIrqCallback([this](bool asserted) {
        m_adbIrqPending = asserted;
        m_via1.setPortBInputBit(3, !m_adbIrqPending);
        updateInterrupts();
    });
    m_via2.setPortAChangedCallback([this](std::uint8_t value) {
        m_glueRamSize = value & 0xc0;
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
    if (!m_swim.loadFloppyImage(path, readOnly)) return false;
    m_floppyPath = path;
    return true;
}

void MacIIcxMachine::ejectFloppyImage()
{
    m_swim.ejectFloppyImage();
    m_floppyPath.clear();
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
    updateViaInputs();
    m_cpu.reset();
    updateInterrupts();
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
    return (static_cast<std::uint32_t>(read16(address)) << 16) | read16(address + 2);
}

void MacIIcxMachine::write8(std::uint32_t address, std::uint8_t value)
{
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
    case 1: case 16: case 32: case 64:
        noMirror = true;
        break;
    case 2:
        bankASize = bankBSize = 1U * 1024U * 1024U;
        break;
    case 4:
        noMirror = true;
        bankASize = bankBSize = 2U * 1024U * 1024U;
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

    const std::size_t bankBLocation = std::size_t {1} << (20 + ((m_glueRamSize >> 6) * 2));
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
    if (devices::nubus::NuBusBus::standardSlot(address) >= 0) {
        ++m_ioStatistics.nubusWrites;
        m_nubus.write32(address, value);
        return;
    }
    write16(address, static_cast<std::uint16_t>(value >> 16));
    write16(address + 2, static_cast<std::uint16_t>(value));
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
        const auto dack = (offset & 0x130) == 0x130;
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
        const auto dack = (offset & 0x130) == 0x130;
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
        for (int slot = 9; slot <= 14 && !integratedVideo; ++slot) {
            integratedVideo = std::dynamic_pointer_cast<devices::video::nubus::CuteMacVideoCard>(m_nubus.card(slot));
        }
        if (integratedVideo && integratedVideo->absolutePointerEnabled()) {
            integratedVideo->setHostPointerPosition(x, y);
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
