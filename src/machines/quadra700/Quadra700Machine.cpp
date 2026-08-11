#include "cutemac/cpu/m68k/M68kFpuDiagnostics.h"
#include "cutemac/machines/quadra700/Quadra700Machine.h"
#include "cutemac/debug/SnapshotBuilder.h"

#include <algorithm>

#include <QFile>

#include "cutemac/devices/video/nubus/CuteMacAcceleratedVideoCard.h"
#include "cutemac/devices/video/nubus/CuteMacVideoCard.h"
#include "cutemac/rom/RomPatcher.h"

namespace cutemac::machines::quadra700 {
namespace {

constexpr std::uint32_t romBase = 0x40000000U;
constexpr std::uint32_t macRomLoadBase = 0x40800000U;
constexpr std::uint32_t ioBase = 0x50000000U;
constexpr std::uint32_t ioMirrorMask = 0x00fc0000U;
constexpr std::uint32_t ioOffsetMask = 0x0003ffffU;
constexpr std::uint32_t dafbVramBase = 0xf9000000U;
constexpr std::uint32_t dafbVramSize = 2U * 1024U * 1024U;
constexpr std::uint32_t dafbRegisterBase = 0xf9800000U;
constexpr std::uint32_t romSize = 1024U * 1024U;
constexpr int cpuToViaRatio = 32;

std::uint8_t highByte(std::uint16_t value) { return static_cast<std::uint8_t>(value >> 8); }
std::uint8_t lowByte(std::uint16_t value) { return static_cast<std::uint8_t>(value); }
std::uint8_t wordHandlerRegister(std::uint32_t offset) { return static_cast<std::uint8_t>((offset >> 9) & 0x0fU); }
std::uint32_t readBigEndian32(const QByteArray& bytes, qsizetype offset)
{
    return (static_cast<std::uint32_t>(static_cast<std::uint8_t>(bytes[offset])) << 24U)
        | (static_cast<std::uint32_t>(static_cast<std::uint8_t>(bytes[offset + 1])) << 16U)
        | (static_cast<std::uint32_t>(static_cast<std::uint8_t>(bytes[offset + 2])) << 8U)
        | static_cast<std::uint32_t>(static_cast<std::uint8_t>(bytes[offset + 3]));
}

std::uint16_t readBigEndian16(const QVector<std::uint8_t>& bytes, qsizetype offset)
{
    return static_cast<std::uint16_t>((static_cast<std::uint16_t>(bytes[offset]) << 8U) | bytes[offset + 1]);
}

void writeBigEndian32(QVector<std::uint8_t>& bytes, qsizetype offset, std::uint32_t value)
{
    bytes[offset] = static_cast<std::uint8_t>(value >> 24U);
    bytes[offset + 1] = static_cast<std::uint8_t>(value >> 16U);
    bytes[offset + 2] = static_cast<std::uint8_t>(value >> 8U);
    bytes[offset + 3] = static_cast<std::uint8_t>(value);
}

void writeBigEndian16(QVector<std::uint8_t>& bytes, qsizetype offset, std::uint16_t value)
{
    bytes[offset] = static_cast<std::uint8_t>(value >> 8U);
    bytes[offset + 1] = static_cast<std::uint8_t>(value);
}

constexpr std::uint8_t bitswapMacAddress(std::uint8_t value)
{
    return static_cast<std::uint8_t>((value & 0x0fU) | ((value & 0x10U) << 3U)
        | ((value & 0x20U) << 1U) | ((value & 0x40U) >> 1U) | ((value & 0x80U) >> 3U));
}

} // namespace

Quadra700Machine::Quadra700Machine(std::size_t ramSize, const QString& nvramPath)
    : m_ram(static_cast<qsizetype>(std::max<std::size_t>(ramSize, 4U * 1024U * 1024U)), 0)
    , m_rom(romSize, 0)
    , m_dafb(devices::video::DafbVideo::Variant::Discrete, devices::video::DafbVideo::Monitor::HiResRgb)
{
    (void)m_rtc.setNvramImagePath(nvramPath);
    m_cpu.setModel(cpu::m68k::M68kCpuCore::Model::M68040);
    m_cpu.setFpuModel(cpu::m68k::M68kCpuCore::FpuModel::M68040);
    m_cpu.setBus(this);

    m_dafb.attachTurboScsi(0, &m_scsi);
    m_dafb.setIrqCallback([this](bool asserted) {
        const auto mask = 0x40U;
        if (asserted) m_nubusIrqState &= static_cast<std::uint8_t>(~mask);
        else m_nubusIrqState |= mask;
        updateViaInputs();
        if (asserted) {
            m_via2.setCa1(true);
            m_via2.setCa1(false);
        } else {
            m_via2.setCa1(true);
        }
        updateInterrupts();
    });

    m_via1.setPowerOnState(0, 0, 0, 0);
    m_via2.setPowerOnState(0, 0, 0, 0);
    m_via1.setAutomaticCa1Period(0);
    m_via2.setAutomaticCa1Period(0);
    m_via1.setPortAChangedCallback([this](std::uint8_t value) {
        m_swim.setSideSelect((value & 0x20U) != 0);
    });
    m_via1.setPortBChangedCallback([this](std::uint8_t value, std::uint8_t ddr) {
        m_adbTransceiver.setViaState(static_cast<std::uint8_t>((value >> 4) & 3U));
        m_rtc.setPins((value & 0x04U) != 0, (value & 0x02U) != 0, (value & 0x01U) != 0 && (ddr & 0x01U) != 0);
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

    m_via2.setPortBChangedCallback([this](std::uint8_t value, std::uint8_t) {
        m_via1.setCa1((value & 0x80U) != 0);
    });
    m_nubus.setSlotIrqCallback([this](int slot, bool asserted) {
        if (slot < 9 || slot > 14) return;
        const auto mask = static_cast<std::uint8_t>(1U << (slot - 9));
        if (asserted) m_nubusIrqState &= static_cast<std::uint8_t>(~mask);
        else m_nubusIrqState |= mask;
        updateViaInputs();
        if (asserted) {
            m_via2.setCa1(true);
            m_via2.setCa1(false);
        } else {
            m_via2.setCa1(true);
        }
        updateInterrupts();
    });
    updateViaInputs();
}

QString Quadra700Machine::machineId() const { return QStringLiteral("quadra-700"); }

void Quadra700Machine::attachSerialEndpoint(int channel, std::shared_ptr<devices::serial::SerialEndpoint> endpoint)
{
    m_scc.attachEndpoint(channel == 0 ? devices::scc::Z8530Scc::Channel::A : devices::scc::Z8530Scc::Channel::B, std::move(endpoint));
}

bool Quadra700Machine::loadRomFile(const QString& path, const QStringList& patches)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return false;
    auto bytes = file.readAll();
    if (bytes.size() != romSize) return false;
    const auto patchResult = rom::RomPatcher::apply(bytes, machineId(), patches);
    if (!patchResult.success) return false;
    m_rom = std::move(bytes);
    m_romLoaded = true;
    return true;
}

bool Quadra700Machine::loadDiskImage(const QString& path) { return loadScsiDisk(0, path, false); }
void Quadra700Machine::ejectDiskImage() { ejectScsiDevice(0); }

bool Quadra700Machine::loadScsiDisk(int id, const QString& path, bool readOnly)
{
    if (id < 0 || id >= static_cast<int>(m_scsiDisks.size())) return false;
    auto disk = std::make_shared<devices::scsi::ScsiBlockDevice>();
    if (!disk->loadImage(path, readOnly)) return false;
    m_scsiCdRoms[static_cast<std::size_t>(id)].reset();
    m_scsiDisks[static_cast<std::size_t>(id)] = disk;
    m_scsi.attachTarget(static_cast<std::uint8_t>(id), disk);
    return true;
}

bool Quadra700Machine::loadScsiCdRom(int id, const QString& path)
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

void Quadra700Machine::ejectScsiCdRom(int id)
{
    if (id < 0 || id >= static_cast<int>(m_scsiCdRoms.size())) return;
    const auto& cdRom = m_scsiCdRoms[static_cast<std::size_t>(id)];
    if (cdRom) cdRom->eject();
}

void Quadra700Machine::ejectScsiDevice(int id)
{
    if (id < 0 || id >= static_cast<int>(m_scsiDisks.size())) return;
    m_scsi.detachTarget(static_cast<std::uint8_t>(id));
    m_scsiDisks[static_cast<std::size_t>(id)].reset();
    m_scsiCdRoms[static_cast<std::size_t>(id)].reset();
}

bool Quadra700Machine::loadFloppyImage(const QString& path, bool readOnly)
{
    return loadFloppyImage(0, path, readOnly);
}

bool Quadra700Machine::loadFloppyImage(int drive, const QString& path, bool readOnly)
{
    if (drive < 0 || drive >= static_cast<int>(m_floppyPaths.size())) return false;
    if (!m_swim.loadFloppyImage(drive, path, readOnly)) return false;
    m_floppyPaths[static_cast<std::size_t>(drive)] = path;
    return true;
}

void Quadra700Machine::ejectFloppyImage() { ejectFloppyImage(0); }

void Quadra700Machine::ejectFloppyImage(int drive)
{
    if (drive < 0 || drive >= static_cast<int>(m_floppyPaths.size())) return;
    m_swim.ejectFloppyImage(drive);
    m_floppyPaths[static_cast<std::size_t>(drive)].clear();
}

void Quadra700Machine::reset()
{
    std::fill(m_ram.begin(), m_ram.end(), 0);
    m_scheduler.reset();
    m_scc.reset();
    m_swim.reset();
    m_asc.reset();
    m_adbTransceiver.reset();
    m_scsi.reset();
    m_dafb.reset();
    m_nubus.reset();
    for (std::size_t id = 0; id < m_scsiDisks.size(); ++id) {
        if (m_scsiDisks[id]) m_scsi.attachTarget(static_cast<std::uint8_t>(id), m_scsiDisks[id]);
        else if (m_scsiCdRoms[id]) {
            m_scsiCdRoms[id]->acknowledgeMediaChange();
            m_scsi.attachTarget(static_cast<std::uint8_t>(id), m_scsiCdRoms[id]);
        }
    }
    m_dafb.attachTurboScsi(0, &m_scsi);
    m_via1.reset();
    m_via2.reset();
    m_overlay = false;
    m_adbIrqPending = false;
    m_hostMousePositionValid = false;
    m_nubusIrqState = 0xff;
    m_viaCycleRemainder = 0;
    m_via1TimerCalibrationState = 0;
    if (m_romLoaded && m_ram.size() >= 8) {
        writeBigEndian32(m_ram, 0, readBigEndian32(m_rom, 0));
        writeBigEndian32(m_ram, 4, macRomLoadBase + readBigEndian32(m_rom, 4));
    }
    rebuildPhysicalMemoryMap();
    updateViaInputs();
    m_cpu.reset();
    updateInterrupts();
}

bool Quadra700Machine::triggerProgrammersInterrupt()
{
    m_cpu.setIrqLevel(0);
    m_cpu.setIrqLevel(7);
    return true;
}

int Quadra700Machine::runCycles(int cycles)
{
    int used = 0;
    while (used < cycles) used += stepInstruction();
    return used;
}

int Quadra700Machine::stepInstruction()
{
    m_scheduler.dispatchDue();
    updateInterrupts();
    const auto instructionCycles = std::max(1, m_cpu.stepInstruction());
    advanceDevices(instructionCycles);
    m_scheduler.advance(static_cast<std::uint64_t>(instructionCycles));
    return instructionCycles;
}

std::uint64_t Quadra700Machine::cycleCount() const { return m_scheduler.now(); }
std::uint32_t Quadra700Machine::programCounter() const { return m_cpu.programCounter(); }

std::uint64_t Quadra700Machine::diskActivityCounter() const
{
    std::uint64_t activity = m_swim.activityCounter();
    for (const auto& disk : m_scsiDisks) if (disk) activity += disk->activityCounter();
    for (const auto& cdRom : m_scsiCdRoms) if (cdRom) activity += cdRom->activityCounter();
    return activity;
}

bool Quadra700Machine::overlayEnabled() const { return m_overlay; }
QByteArray Quadra700Machine::framebufferBytes() const { return videoFrame().pixels; }

devices::video::VideoFrame Quadra700Machine::videoFrame() const
{
    const auto onboard = m_dafb.videoFrame();
    if (onboard.valid()) return onboard;
    for (int slot = 13; slot <= 14; ++slot) {
        const auto card = m_nubus.card(slot);
        if (!card) continue;
        const auto frame = card->videoFrame();
        if (frame.valid()) return frame;
    }
    return {};
}

void Quadra700Machine::queueInput(const core::GuestInputEvent& event, std::uint64_t cycle)
{
    m_scheduler.schedule(std::max(cycle, m_scheduler.now()), [this, event]() { applyInput(event); }, "guest-input");
}

std::uint8_t Quadra700Machine::read8(std::uint32_t address)
{
    if (m_overlay && address < static_cast<std::uint32_t>(m_rom.size())) return static_cast<std::uint8_t>(m_rom[address]);
    if ((address & 0xf0000000U) == romBase) {
        if (m_overlay) {
            m_overlay = false;
            rebuildPhysicalMemoryMap();
        }
        return static_cast<std::uint8_t>(m_rom[(address - romBase) & (m_rom.size() - 1)]);
    }
    std::uint8_t directValue = 0;
    if (m_physicalMemoryMap.tryRead8(address, directValue)) return directValue;
    if (!m_overlay) {
        if (const auto index = ramIndex(address)) return m_ram[static_cast<qsizetype>(*index)];
        if (address < 0x40000000U) return 0x00;
    }
    if (isDafbVram(address)) return m_dafb.readVram8(address - dafbVramBase);
    if (isDafbRegister(address)) return m_dafb.readRegister8(address - dafbRegisterBase);
    if (isIo(address)) return readIo8(address);
    if (devices::nubus::NuBusBus::standardSlot(address) >= 0 || devices::nubus::NuBusBus::superSlot(address) >= 0) {
        return m_nubus.read8(address);
    }
    return 0xff;
}

std::uint16_t Quadra700Machine::read16(std::uint32_t address)
{
    if (m_overlay && address < static_cast<std::uint32_t>(m_rom.size() - 1))
        return static_cast<std::uint16_t>((read8(address) << 8) | read8(address + 1));
    if ((address & 0xf0000000U) == romBase)
        return static_cast<std::uint16_t>((read8(address) << 8) | read8(address + 1));
    std::uint16_t directValue = 0;
    if (m_physicalMemoryMap.tryRead16(address, directValue)) return directValue;
    if (isDafbVram(address)) return m_dafb.readVram16(address - dafbVramBase);
    if (isDafbRegister(address)) return m_dafb.readRegister16(address - dafbRegisterBase);
    if (isIo(address) && ((address & ioOffsetMask) >= 0xf100) && ((address & ioOffsetMask) < 0xf102))
        return m_dafb.readTurboScsiDma16(0);
    if (isIo(address)) {
        const auto value = readIo8(address);
        return static_cast<std::uint16_t>((value << 8) | value);
    }
    if (devices::nubus::NuBusBus::standardSlot(address) >= 0 || devices::nubus::NuBusBus::superSlot(address) >= 0) {
        return m_nubus.read16(address);
    }
    return static_cast<std::uint16_t>((read8(address) << 8) | read8(address + 1));
}

std::uint32_t Quadra700Machine::read32(std::uint32_t address)
{
    if ((address & 0xf0000000U) == romBase)
        return (static_cast<std::uint32_t>(read16(address)) << 16U) | read16(address + 2);
    std::uint32_t directValue = 0;
    if (!m_overlay && m_physicalMemoryMap.tryRead32(address, directValue)) return directValue;
    if (isDafbVram(address)) return m_dafb.readVram32(address - dafbVramBase);
    if (isDafbRegister(address)) return m_dafb.readRegister32(address - dafbRegisterBase);
    if (devices::nubus::NuBusBus::standardSlot(address) >= 0 || devices::nubus::NuBusBus::superSlot(address) >= 0) {
        return m_nubus.read32(address);
    }
    return (static_cast<std::uint32_t>(read16(address)) << 16U) | read16(address + 2);
}

void Quadra700Machine::write8(std::uint32_t address, std::uint8_t value)
{
    if (m_physicalMemoryMap.tryWrite8(address, value)) return;
    if (!m_overlay) {
        if (const auto index = ramIndex(address)) {
            m_ram[static_cast<qsizetype>(*index)] = value;
            return;
        }
        if (address < 0x40000000U) return;
    }
    if (isDafbVram(address)) {
        m_dafb.writeVram8(address - dafbVramBase, value);
    } else if (isDafbRegister(address)) {
        m_dafb.writeRegister8(address - dafbRegisterBase, value);
    } else if (isIo(address)) {
        writeIo8(address, value);
    } else if (devices::nubus::NuBusBus::standardSlot(address) >= 0 || devices::nubus::NuBusBus::superSlot(address) >= 0) {
        m_nubus.write8(address, value);
    }
}

void Quadra700Machine::write16(std::uint32_t address, std::uint16_t value)
{
    if (m_physicalMemoryMap.tryWrite16(address, value)) return;
    if (isDafbVram(address)) {
        m_dafb.writeVram16(address - dafbVramBase, value);
    } else if (isDafbRegister(address)) {
        m_dafb.writeRegister16(address - dafbRegisterBase, value);
    } else if (isIo(address) && ((address & ioOffsetMask) >= 0xf100) && ((address & ioOffsetMask) < 0xf102)) {
        m_dafb.writeTurboScsiDma16(0, value);
    } else if (isIo(address)) {
        writeIo8(address, highByte(value));
    } else if (devices::nubus::NuBusBus::standardSlot(address) >= 0 || devices::nubus::NuBusBus::superSlot(address) >= 0) {
        m_nubus.write16(address, value);
    } else {
        write8(address, highByte(value));
        write8(address + 1, lowByte(value));
    }
}

void Quadra700Machine::write32(std::uint32_t address, std::uint32_t value)
{
    if (m_physicalMemoryMap.tryWrite32(address, value)) return;
    if (isDafbVram(address)) {
        m_dafb.writeVram32(address - dafbVramBase, value);
    } else if (isDafbRegister(address)) {
        m_dafb.writeRegister32(address - dafbRegisterBase, value);
    } else if (devices::nubus::NuBusBus::standardSlot(address) >= 0 || devices::nubus::NuBusBus::superSlot(address) >= 0) {
        m_nubus.write32(address, value);
    } else {
        write16(address, static_cast<std::uint16_t>(value >> 16U));
        write16(address + 2, static_cast<std::uint16_t>(value));
    }
}

std::optional<std::size_t> Quadra700Machine::ramIndex(std::uint32_t address) const
{
    if (address < static_cast<std::uint32_t>(m_ram.size())) return address;
    return std::nullopt;
}

void Quadra700Machine::rebuildPhysicalMemoryMap()
{
    m_physicalMemoryMap.clear();
    if (!m_overlay) {
        for (std::uint32_t address = 0; address < static_cast<std::uint32_t>(m_ram.size());
             address += core::PhysicalMemoryMap::pageSize) {
            const auto index = ramIndex(address);
            if (index && *index + core::PhysicalMemoryMap::pageSize <= static_cast<std::size_t>(m_ram.size()))
                m_physicalMemoryMap.mapReadWritePage(address, m_ram.data() + *index);
        }
    }
    m_physicalMemoryMap.mapReadOnlyMirrored(romBase, 0x10000000U,
        reinterpret_cast<const std::uint8_t*>(m_rom.constData()), static_cast<std::uint32_t>(m_rom.size()));
}

bool Quadra700Machine::installNuBusCard(int slot, std::shared_ptr<devices::nubus::NuBusCard> card)
{
    return slot >= 13 && slot <= 14 && m_nubus.install(slot, std::move(card));
}

QString Quadra700Machine::debugCpuArchitecture() const { return QStringLiteral("m68k:68040"); }

QStringList Quadra700Machine::debugRegisterLines() const
{
    const auto regs = m_cpu.registers();
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

QString Quadra700Machine::disassemble(std::uint32_t address) const { return m_cpu.disassemble(address); }
int Quadra700Machine::disassembleBytes(std::uint32_t address) const { return m_cpu.disassembleBytes(address); }
std::uint8_t Quadra700Machine::debugRead8(std::uint32_t address) const { return const_cast<Quadra700Machine*>(this)->read8(address); }
std::uint16_t Quadra700Machine::debugRead16(std::uint32_t address) const { return const_cast<Quadra700Machine*>(this)->read16(address); }
std::uint32_t Quadra700Machine::debugRead32(std::uint32_t address) const { return const_cast<Quadra700Machine*>(this)->read32(address); }
void Quadra700Machine::debugWrite8(std::uint32_t address, std::uint8_t value) { write8(address, value); }
void Quadra700Machine::debugWrite16(std::uint32_t address, std::uint16_t value) { write16(address, value); }
void Quadra700Machine::debugWrite32(std::uint32_t address, std::uint32_t value) { write32(address, value); }

bool Quadra700Machine::isIo(std::uint32_t address) const
{
    return (address & ~ioMirrorMask & 0xfff00000U) == ioBase;
}

bool Quadra700Machine::isDafbRegister(std::uint32_t address) const
{
    return address >= dafbRegisterBase && address < dafbRegisterBase + 0x400U;
}

bool Quadra700Machine::isDafbVram(std::uint32_t address) const
{
    return address >= dafbVramBase && address < dafbVramBase + dafbVramSize;
}

std::uint8_t Quadra700Machine::readIo8(std::uint32_t address)
{
    const auto offset = address & ioOffsetMask;
    if (offset < 0x2000) return m_via1.readRegister(wordHandlerRegister(offset));
    if (offset >= 0x2000 && offset < 0x4000) return m_via2.readRegister(wordHandlerRegister(offset - 0x2000));
    if (offset >= 0x8000 && offset < 0x8008) {
        static constexpr std::array<std::uint8_t, 6> mac {
            bitswapMacAddress(0x00), bitswapMacAddress(0x05), bitswapMacAddress(0x02),
            bitswapMacAddress(0x00), bitswapMacAddress(0x70), bitswapMacAddress(0x01),
        };
        const auto index = offset - 0x8000;
        if (index < mac.size()) return mac[index];
        std::uint8_t sum = 0;
        for (const auto byte : mac) sum ^= byte;
        return sum ^ 0xffU;
    }
    if (offset >= 0xc000 && offset < 0xe000) {
        using Channel = devices::scc::Z8530Scc::Channel;
        switch ((offset - 0xc000) & 6U) {
        case 6: return m_scc.readData(Channel::A);
        case 4: return m_scc.readData(Channel::B);
        case 2: return m_scc.readControl(Channel::A);
        default: return m_scc.readControl(Channel::B);
        }
    }
    if (offset >= 0xf000 && offset < 0xf100) return m_dafb.readTurboScsiRegister(0, offset - 0xf000);
    if (offset >= 0xf100 && offset < 0xf102) return highByte(m_dafb.readTurboScsiDma16(0));
    if (offset >= 0x14000 && offset < 0x16000) return m_asc.read(static_cast<std::uint16_t>(offset & 0x0fffU));
    if (offset >= 0x1e000 && offset < 0x20000) return m_swim.access(wordHandlerRegister(offset - 0x1e000));
    return 0x00;
}

void Quadra700Machine::writeIo8(std::uint32_t address, std::uint8_t value)
{
    const auto offset = address & ioOffsetMask;
    if (offset < 0x2000) {
        const auto reg = wordHandlerRegister(offset);
        observeVia1TimerCalibrationWrite(reg, value);
        m_via1.writeRegister(reg, value);
    } else if (offset >= 0x2000 && offset < 0x4000) {
        m_via2.writeRegister(wordHandlerRegister(offset - 0x2000), value);
    } else if (offset >= 0xc000 && offset < 0xe000) {
        using Channel = devices::scc::Z8530Scc::Channel;
        switch ((offset - 0xc000) & 6U) {
        case 6: m_scc.writeData(Channel::A, value); break;
        case 4: m_scc.writeData(Channel::B, value); break;
        case 2: m_scc.writeControl(Channel::A, value); break;
        default: m_scc.writeControl(Channel::B, value); break;
        }
    } else if (offset >= 0xf000 && offset < 0xf100) {
        m_dafb.writeTurboScsiRegister(0, offset - 0xf000, value);
    } else if (offset >= 0xf100 && offset < 0xf102) {
        m_dafb.writeTurboScsiDma16(0, static_cast<std::uint16_t>((value << 8) | value));
    } else if (offset >= 0x14000 && offset < 0x16000) {
        m_asc.write(static_cast<std::uint16_t>(offset & 0x0fffU), value);
    } else if (offset >= 0x1e000 && offset < 0x20000) {
        (void)m_swim.access(wordHandlerRegister(offset - 0x1e000), value, true);
    }
    updateInterrupts();
}

void Quadra700Machine::observeVia1TimerCalibrationWrite(std::uint8_t reg, std::uint8_t value)
{
    // The Quadra ROM calibrates delay loops against VIA1 Timer 2. On a host
    // interpreter, that measurement is not meaningful; QEMU recognizes this
    // exact programming sequence and supplies calibrated low-memory values.
    // Keep it local to this chipset rather than distorting the reusable VIA.
    constexpr std::uint8_t peripheralControlRegister = 12;
    constexpr std::uint8_t timer2CounterLow = 8;
    constexpr std::uint8_t timer2CounterHigh = 9;
    constexpr std::uint8_t interruptEnableRegister = 14;

    switch (m_via1TimerCalibrationState) {
    case 0:
        if (reg == peripheralControlRegister && value == 0x22) m_via1TimerCalibrationState = 1;
        break;
    case 1:
        if (reg == timer2CounterLow && value == 0x0c) {
            m_via1TimerCalibrationState = readBigEndian16(m_ram, 0x0d00) == 0x7e00
                    && readBigEndian16(m_ram, 0x0d02) == 0x16d7
                ? 0
                : 2;
        }
        break;
    case 2:
        if (reg == timer2CounterHigh && value == 0x03) {
            m_via1TimerCalibrationState = readBigEndian16(m_ram, 0x0d00) == 0x7e00
                    && readBigEndian16(m_ram, 0x0d02) == 0x16d7
                ? 0
                : 3;
        }
        break;
    case 3:
        if (reg == interruptEnableRegister && value == 0x20) {
            writeBigEndian16(m_ram, 0x0d00, static_cast<std::uint16_t>(0x2a00 * 3));
            writeBigEndian16(m_ram, 0x0d02, static_cast<std::uint16_t>(0x079d * 3));
            m_via1TimerCalibrationState = 4;
        }
        break;
    default:
        if (reg == peripheralControlRegister && value == 0x22) m_via1TimerCalibrationState = 1;
        break;
    }
}

void Quadra700Machine::applyInput(const core::GuestInputEvent& event)
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
        for (int slot = 13; slot <= 14 && !integratedVideo && !acceleratedVideo; ++slot) {
            integratedVideo = std::dynamic_pointer_cast<devices::video::nubus::CuteMacVideoCard>(m_nubus.card(slot));
            acceleratedVideo = std::dynamic_pointer_cast<devices::video::nubus::CuteMacAcceleratedVideoCard>(m_nubus.card(slot));
        }
        if (integratedVideo && integratedVideo->absolutePointerEnabled()) integratedVideo->setHostPointerPosition(x, y);
        else if (acceleratedVideo && acceleratedVideo->absolutePointerEnabled()) acceleratedVideo->setHostPointerPosition(x, y);
        else if (m_hostMousePositionValid) {
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

void Quadra700Machine::updateInterrupts()
{
    m_via2.setCb1(!m_asc.interruptActive());
    m_via2.setCb2(!m_scsi.interruptActive());
    m_dafb.setTurboScsiDrq(0, m_scsi.dmaRequest());
    const auto level = m_scc.interruptActive() ? 4U
        : (m_via2.interruptActive() ? 2U : (m_via1.interruptActive() ? 1U : 0U));
    m_cpu.setIrqLevel(level);
}

void Quadra700Machine::updateViaInputs()
{
    const std::uint8_t via1A = 0xc1;
    const std::uint8_t via1B = static_cast<std::uint8_t>(m_rtc.dataLine() | (m_adbIrqPending ? 0x00U : 0x08U));
    const auto via2A = static_cast<std::uint8_t>(0x80U | m_nubusIrqState);
    const std::uint8_t via2B = 0xcf;
    for (std::uint8_t bit = 0; bit < 8; ++bit) {
        m_via1.setPortAInputBit(bit, (via1A & (1U << bit)) != 0);
        m_via1.setPortBInputBit(bit, (via1B & (1U << bit)) != 0);
        m_via2.setPortAInputBit(bit, (via2A & (1U << bit)) != 0);
        m_via2.setPortBInputBit(bit, (via2B & (1U << bit)) != 0);
    }
}

void Quadra700Machine::advanceDevices(int cpuCycles)
{
    m_scc.tick(cpuCycles);
    m_asc.tick(static_cast<std::uint64_t>(cpuCycles));
    m_adbTransceiver.tick(cpuCycles);
    m_dafb.tick(static_cast<std::uint64_t>(cpuCycles));
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


debug::MachineSnapshot Quadra700Machine::debugSnapshot() const
{
    debug::MachineSnapshot snapshot;
    snapshot.machineId = machineId();
    snapshot.cycle = cycleCount();
    snapshot.overlayEnabled = m_overlay;
    snapshot.romLoaded = m_romLoaded;

    const debug::MemoryReader read8 = [this](std::uint32_t address) { return debugRead8(address); };
    const debug::Disassembler disassemble = [this](std::uint32_t address) {
        return qMakePair(this->disassemble(address), disassembleBytes(address));
    };
    snapshot.cpu = debug::buildCpuSnapshot(cpuRegisters(), debugCpuArchitecture(), read8, disassemble);

    debug::MemoryRegion ram;
    ram.name = QStringLiteral("ram");
    ram.kind = QStringLiteral("ram");
    ram.base = 0;
    ram.length = static_cast<std::uint32_t>(m_ram.size());
    ram.writable = true;
    ram.contentsMember = QStringLiteral("mem/ram.bin");
    ram.contents = QByteArray(reinterpret_cast<const char*>(m_ram.constData()),
        static_cast<qsizetype>(m_ram.size()));
    snapshot.memory.append(ram);

    debug::MemoryRegion rom;
    rom.name = QStringLiteral("rom");
    rom.kind = QStringLiteral("rom");
    rom.base = romBase;
    rom.length = static_cast<std::uint32_t>(m_rom.size());
    // ROM answers across the whole mirrored window (see mapReadOnlyMirrored),
    // and the reset vector and ROMBase both point above the first copy, so a
    // dump that mapped only one copy cannot disassemble at the captured PC.
    rom.decodeLength = 0x10000000U;
    rom.contentsMember = QStringLiteral("mem/rom.bin");
    rom.contents = m_rom;
    snapshot.memory.append(rom);

    debug::MemoryRegion vram;
    vram.name = QStringLiteral("vram-dafb");
    vram.kind = QStringLiteral("vram");
    vram.base = dafbVramBase;
    vram.length = dafbVramSize;
    vram.writable = true;
    vram.contentsMember = QStringLiteral("mem/vram-dafb.bin");
    vram.contents = m_dafb.vramBytes();
    snapshot.memory.append(vram);

    snapshot.devices.append(debug::viaSnapshot(QStringLiteral("via1"), m_via1.debugState()));
    snapshot.devices.append(debug::viaSnapshot(QStringLiteral("via2"), m_via2.debugState()));
    snapshot.devices.append(debug::scsiSnapshot(QStringLiteral("scsi"), m_scsi.debugState()));
    snapshot.devices.append(debug::iwmSnapshot(QStringLiteral("swim"), m_swim.debugState()));
    snapshot.devices.append(debug::sccSnapshot(QStringLiteral("scc-a"),
        m_scc.debugState(devices::scc::Z8530Scc::Channel::A), m_scc.interruptActive()));
    snapshot.devices.append(debug::sccSnapshot(QStringLiteral("scc-b"),
        m_scc.debugState(devices::scc::Z8530Scc::Channel::B), m_scc.interruptActive()));
    snapshot.devices.append(debug::adbSnapshot(QStringLiteral("adb"), m_adbTransceiver.debugState()));
    snapshot.devices.append(debug::rtcSnapshot(QStringLiteral("rtc"), m_rtc));
    snapshot.devices.append(debug::makeVideoSnapshot(QStringLiteral("dafb"),
        QStringLiteral("dafb-video"), m_dafb.videoFrame(), {}));
    snapshot.devices.append(debug::lowMemorySnapshot(read8));
    snapshot.devices.append(debug::nubusSnapshots(m_nubus));

    snapshot.frame = videoFrame();
    snapshot.schedulerEvents = m_scheduler.pendingEvents();
    // Floating-point encodings the FPU refused. A guest that bombs with system
    // error 10 leaves no exception frame behind by the time the dialog is up,
    // so without this a dump cannot name the instruction that did it.
    const auto fpuDiagnostics = cpu::m68k::fpuDiagnosticLines();
    if (!fpuDiagnostics.isEmpty()) {
        snapshot.traces.insert(QStringLiteral("fpu-refused"), fpuDiagnostics);
    }
    const auto swimTrace = m_swim.traceEvents();
    if (!swimTrace.isEmpty()) snapshot.traces.insert(QStringLiteral("swim"), swimTrace);
    return snapshot;
}

} // namespace cutemac::machines::quadra700
