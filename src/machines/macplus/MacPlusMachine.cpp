#include "cutemac/machines/macplus/MacPlusMachine.h"

#include <QFile>
#include <QString>

#include <algorithm>

namespace cutemac::machines::macplus {

namespace {

constexpr std::uint32_t ramBase = 0x000000;
constexpr std::uint32_t romBase = 0x400000;
constexpr std::uint32_t overlayRamBase = 0x600000;
constexpr std::uint32_t scsiBase = 0x580000;
constexpr std::uint32_t sccReadBase = 0x9ffff8;
constexpr std::uint32_t sccWriteBase = 0xbffff9;
constexpr std::uint32_t iwmBase = 0xdfe1ff;
constexpr std::uint32_t viaBase = 0xefe1fe;
constexpr std::uint32_t ramConfigBase = 0xf00000;
constexpr std::uint32_t diagnosticVectorBase = 0xf80000;
constexpr std::uint32_t lowMemoryTicks = 0x00016a;
constexpr std::uint32_t screenBase4MiB = 0x3fa700;
constexpr std::uint32_t screenBytes = 512 * 342 / 8;
constexpr std::uint32_t soundBase4MiB = 0x3ffd00;
constexpr std::uint32_t soundBytes = 370;
constexpr qsizetype maxBusTraceEntries = 4096;
constexpr qsizetype maxSoundCaptureBytes = 22255 * 30;

constexpr std::uint32_t regionMask = 0xc00000;
constexpr std::uint32_t offset4MiBMask = 0x3fffff;

constexpr std::uint8_t viaOverlayBit = 0x10;
constexpr std::uint8_t viaDiskSelectBit = 0x20;

[[nodiscard]] std::uint8_t highByte(std::uint16_t value)
{
    return static_cast<std::uint8_t>(value >> 8);
}

[[nodiscard]] std::uint8_t lowByte(std::uint16_t value)
{
    return static_cast<std::uint8_t>(value);
}

} // namespace

MacPlusMachine::MacPlusMachine(std::size_t ramSize)
    : m_ram(static_cast<qsizetype>(ramSize), 0)
{
    m_cpu.setModel(cpu::m68k::M68kCpuCore::Model::M68000);
    m_cpu.setBus(this);

    m_via.setPortAChangedCallback([this](std::uint8_t portA) {
        setOverlayEnabled((portA & viaOverlayBit) != 0);
    });
    m_iwm.setDiskSenseProvider([this](std::uint8_t caLines) {
        const auto sel = (m_via.portA() & viaDiskSelectBit) != 0;
        switch (caLines & 0x07) {
        case 0x0:
            return !sel; // DIRTN high; CSTIN low means no disk inserted.
        case 0x1: // STEP
        case 0x3: // WRTPRT
        case 0x4: // MOTORON
        case 0x5: // TKO
            return true;
        case 0x6:
            return !sel; // SIDES low for double-sided; RDDATA0 idle high.
        case 0x7:
            return sel; // DRVIN high, TACH low while no disk is spinning.
        case 0x2:
        default:
            return true;
        }
    });
}

bool MacPlusMachine::loadRomFile(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return false;
    }

    const auto data = file.readAll();
    if (data.size() != static_cast<qsizetype>(m_rom.size())) {
        return false;
    }

    std::copy(data.begin(), data.end(), m_rom.begin());
    m_romLoaded = true;
    m_romPath = path;
    return true;
}

void MacPlusMachine::reset()
{
    m_accessSummary = {};
    m_eventLog.clear();
    m_soundCapture.clear();

    std::fill(m_ram.begin(), m_ram.end(), 0);
    m_scc.reset();
    m_iwm.reset();
    m_scsi.reset();
    m_via.reset();
    setOverlayEnabled(true);

    m_cpu.reset();
    logEvent(QStringLiteral("reset pc=0x%1 overlay=%2")
                 .arg(programCounter(), 8, 16, QLatin1Char('0'))
                 .arg(overlayEnabled() ? QStringLiteral("on") : QStringLiteral("off")));
}

int MacPlusMachine::runCycles(int cycles)
{
    return m_cpu.execute(cycles);
}

int MacPlusMachine::stepInstruction()
{
    return m_cpu.stepInstruction();
}

bool MacPlusMachine::runUntilPc(std::uint32_t address, int maxCycles)
{
    int cyclesUsed = 0;
    while (cyclesUsed < maxCycles) {
        if (programCounter() == (address & 0x00ffffff)) {
            return true;
        }
        cyclesUsed += std::max(1, m_cpu.stepInstruction());
    }

    return programCounter() == (address & 0x00ffffff);
}

std::uint32_t MacPlusMachine::programCounter() const
{
    return m_cpu.programCounter();
}

cpu::m68k::M68kCpuCore::RegisterSnapshot MacPlusMachine::cpuRegisters() const
{
    return m_cpu.registers();
}

QString MacPlusMachine::disassemble(std::uint32_t address) const
{
    return m_cpu.disassemble(address);
}

int MacPlusMachine::disassembleBytes(std::uint32_t address) const
{
    return m_cpu.disassembleBytes(address);
}

bool MacPlusMachine::overlayEnabled() const
{
    return m_overlayEnabled;
}

const MacPlusMachine::AccessSummary& MacPlusMachine::accessSummary() const
{
    return m_accessSummary;
}

const QVector<QString>& MacPlusMachine::eventLog() const
{
    return m_eventLog;
}

QVector<MacPlusMachine::BusAccess> MacPlusMachine::busTrace() const
{
    return m_busTrace;
}

void MacPlusMachine::clearBusTrace()
{
    m_busTrace.clear();
}

std::uint8_t MacPlusMachine::read8(std::uint32_t address)
{
    address &= 0x00ffffff;
    const auto region = regionFor(address);
    if (region == Region::Ram) {
        ++m_accessSummary.ramReads;
        const auto value = m_ram[ramOffset(address)];
        recordBusAccess("read", region, address, value, 1);
        return value;
    }
    if (region == Region::Rom) {
        ++m_accessSummary.romReads;
        const auto value = m_rom[romOffset(address)];
        recordBusAccess("read", region, address, value, 1);
        return value;
    }

    const auto value = readDevice8(address, region);
    recordBusAccess("read", region, address, value, 1);
    return value;
}

std::uint16_t MacPlusMachine::read16(std::uint32_t address)
{
    return static_cast<std::uint16_t>((read8(address) << 8) | read8(address + 1));
}

std::uint32_t MacPlusMachine::read32(std::uint32_t address)
{
    address &= 0x00ffffff;
    if (!m_overlayEnabled && address == lowMemoryTicks && regionFor(address) == Region::Ram) {
        ++m_accessSummary.syntheticTickReads;
        incrementLowMemoryTicks();
        m_accessSummary.ramReads += 4;
        return readRam32Direct(address);
    }

    return (static_cast<std::uint32_t>(read16(address)) << 16) | read16(address + 2);
}

void MacPlusMachine::write8(std::uint32_t address, std::uint8_t value)
{
    address &= 0x00ffffff;
    const auto region = regionFor(address);
    if (region == Region::Ram) {
        ++m_accessSummary.ramWrites;
        m_ram[ramOffset(address)] = value;
        recordSoundBufferWrite(address, value);
        recordBusAccess("write", region, address, value, 1);
        return;
    }

    writeDevice8(address, region, value);
    recordBusAccess("write", region, address, value, 1);
}

void MacPlusMachine::write16(std::uint32_t address, std::uint16_t value)
{
    write8(address, highByte(value));
    write8(address + 1, lowByte(value));
}

void MacPlusMachine::write32(std::uint32_t address, std::uint32_t value)
{
    write16(address, static_cast<std::uint16_t>(value >> 16));
    write16(address + 2, static_cast<std::uint16_t>(value));
}

MacPlusMachine::Region MacPlusMachine::regionFor(std::uint32_t address) const
{
    if (m_overlayEnabled && address < m_rom.size()) {
        return Region::Rom;
    }
    if (m_overlayEnabled && address >= overlayRamBase && address < overlayRamBase + static_cast<std::uint32_t>(m_ram.size())) {
        return Region::Ram;
    }
    if (!m_overlayEnabled && address < static_cast<std::uint32_t>(m_ram.size())) {
        return Region::Ram;
    }
    if ((address & regionMask) == romBase) {
        const auto offset = address & offset4MiBMask;
        if (offset >= (scsiBase - romBase) && offset < (scsiBase - romBase + 0x1000)) {
            return Region::Scsi;
        }
        return Region::Rom;
    }
    if ((address & regionMask) == 0x800000) {
        return Region::Scc;
    }
    if ((address & 0xf00000) == 0xd00000) {
        return Region::Iwm;
    }
    if ((address & 0xf00000) == 0xe00000) {
        return Region::Via;
    }
    if (address >= ramConfigBase && address < ramConfigBase + 8) {
        return Region::Configuration;
    }
    if (address >= diagnosticVectorBase && address < diagnosticVectorBase + 0x100) {
        return Region::Configuration;
    }

    return Region::Unmapped;
}

std::uint32_t MacPlusMachine::ramOffset(std::uint32_t address) const
{
    if (m_overlayEnabled && address >= overlayRamBase) {
        return (address - overlayRamBase) % static_cast<std::uint32_t>(m_ram.size());
    }
    return (address - ramBase) % static_cast<std::uint32_t>(m_ram.size());
}

std::uint32_t MacPlusMachine::romOffset(std::uint32_t address) const
{
    if (m_overlayEnabled && address < m_rom.size()) {
        return address % static_cast<std::uint32_t>(m_rom.size());
    }
    return (address - romBase) % static_cast<std::uint32_t>(m_rom.size());
}

std::uint8_t MacPlusMachine::readDevice8(std::uint32_t address, Region region)
{
    switch (region) {
    case Region::Scc: {
        ++m_accessSummary.sccReads;
        const auto offset = static_cast<std::uint8_t>((address - sccReadBase) & 0x07);
        using Channel = devices::scc::Z8530Scc::Channel;
        if (offset == 6) {
            return m_scc.readData(Channel::A);
        }
        if (offset == 4) {
            return m_scc.readData(Channel::B);
        }
        if (offset == 2) {
            return m_scc.readControl(Channel::A);
        }
        return m_scc.readControl(Channel::B);
    }
    case Region::Iwm: {
        ++m_accessSummary.iwmReads;
        const auto registerIndex = static_cast<std::uint8_t>(((address - iwmBase) >> 9) & 0x0f);
        return m_iwm.access(registerIndex);
    }
    case Region::Via: {
        ++m_accessSummary.viaReads;
        const auto registerIndex = static_cast<std::uint8_t>(((address - viaBase) >> 9) & 0x0f);
        return m_via.readRegister(registerIndex);
    }
    case Region::Scsi: {
        ++m_accessSummary.scsiReads;
        const auto registerIndex = static_cast<std::uint8_t>((address >> 4) & 0x07);
        const auto dack = (address & 0x0200) != 0;
        return m_scsi.readRegister(registerIndex, dack);
    }
    case Region::Configuration:
        ++m_accessSummary.configurationReads;
        return 0;
    case Region::Ram:
    case Region::Rom:
    case Region::Unmapped:
        break;
    }

    ++m_accessSummary.unmappedReads;
    logAccess("read unmapped", address, 0xff);
    return 0xff;
}

void MacPlusMachine::writeDevice8(std::uint32_t address, Region region, std::uint8_t value)
{
    switch (region) {
    case Region::Rom:
        return;
    case Region::Scc: {
        ++m_accessSummary.sccWrites;
        const auto offset = static_cast<std::uint8_t>((address - sccWriteBase) & 0x07);
        using Channel = devices::scc::Z8530Scc::Channel;
        if (offset == 6) {
            m_scc.writeData(Channel::A, value);
        } else if (offset == 4) {
            m_scc.writeData(Channel::B, value);
        } else if (offset == 2) {
            m_scc.writeControl(Channel::A, value);
        } else {
            m_scc.writeControl(Channel::B, value);
        }
        return;
    }
    case Region::Iwm: {
        ++m_accessSummary.iwmWrites;
        const auto registerIndex = static_cast<std::uint8_t>(((address - iwmBase) >> 9) & 0x0f);
        (void)m_iwm.access(registerIndex);
        return;
    }
    case Region::Via: {
        ++m_accessSummary.viaWrites;
        const auto registerIndex = static_cast<std::uint8_t>(((address - viaBase) >> 9) & 0x0f);
        m_via.writeRegister(registerIndex, value);
        return;
    }
    case Region::Scsi: {
        ++m_accessSummary.scsiWrites;
        const auto registerIndex = static_cast<std::uint8_t>((address >> 4) & 0x07);
        const auto dack = (address & 0x0200) != 0;
        m_scsi.writeRegister(registerIndex, dack, value);
        return;
    }
    case Region::Configuration:
        return;
    case Region::Ram:
    case Region::Unmapped:
        break;
    }

    ++m_accessSummary.unmappedWrites;
    logAccess("write unmapped", address, value);
}

std::uint8_t MacPlusMachine::debugRead8(std::uint32_t address) const
{
    address &= 0x00ffffff;
    const auto region = regionFor(address);
    if (region == Region::Ram) {
        return m_ram[ramOffset(address)];
    }
    if (region == Region::Rom) {
        return m_rom[romOffset(address)];
    }
    if (region == Region::Configuration) {
        return 0;
    }
    return 0xff;
}

std::uint16_t MacPlusMachine::debugRead16(std::uint32_t address) const
{
    return static_cast<std::uint16_t>((debugRead8(address) << 8) | debugRead8(address + 1));
}

std::uint32_t MacPlusMachine::debugRead32(std::uint32_t address) const
{
    return (static_cast<std::uint32_t>(debugRead16(address)) << 16) | debugRead16(address + 2);
}

void MacPlusMachine::debugWrite8(std::uint32_t address, std::uint8_t value)
{
    write8(address, value);
}

void MacPlusMachine::debugWrite16(std::uint32_t address, std::uint16_t value)
{
    write16(address, value);
}

void MacPlusMachine::debugWrite32(std::uint32_t address, std::uint32_t value)
{
    write32(address, value);
}

QByteArray MacPlusMachine::framebufferBytes() const
{
    QByteArray bytes;
    bytes.resize(screenBytes);
    for (std::uint32_t i = 0; i < screenBytes; ++i) {
        bytes[static_cast<qsizetype>(i)] = static_cast<char>(debugRead8(screenBase4MiB + i));
    }
    return bytes;
}

std::uint32_t MacPlusMachine::framebufferHash() const
{
    const auto bytes = framebufferBytes();
    std::uint32_t hash = 2166136261U;
    for (const auto byte : bytes) {
        hash ^= static_cast<std::uint8_t>(byte);
        hash *= 16777619U;
    }
    return hash;
}

QByteArray MacPlusMachine::soundBufferBytes() const
{
    QByteArray bytes;
    bytes.resize(soundBytes);
    for (std::uint32_t i = 0; i < soundBytes; ++i) {
        bytes[static_cast<qsizetype>(i)] = static_cast<char>(debugRead8(soundBase4MiB + i));
    }
    return bytes;
}

std::uint32_t MacPlusMachine::soundBufferHash() const
{
    const auto bytes = soundBufferBytes();
    std::uint32_t hash = 2166136261U;
    for (const auto byte : bytes) {
        hash ^= static_cast<std::uint8_t>(byte);
        hash *= 16777619U;
    }
    return hash;
}

QByteArray MacPlusMachine::soundCaptureBytes() const
{
    return m_soundCapture;
}

std::uint32_t MacPlusMachine::soundCaptureHash() const
{
    std::uint32_t hash = 2166136261U;
    for (const auto byte : m_soundCapture) {
        hash ^= static_cast<std::uint8_t>(byte);
        hash *= 16777619U;
    }
    return hash;
}

void MacPlusMachine::clearSoundCapture()
{
    m_soundCapture.clear();
}

MacPlusMachine::RomInfo MacPlusMachine::romInfo() const
{
    std::uint32_t checksum = 0;
    for (std::size_t i = 0; i < m_rom.size(); i += 2) {
        checksum += static_cast<std::uint16_t>((m_rom[i] << 8) | m_rom[i + 1]);
    }

    return {
        m_romPath,
        static_cast<std::uint32_t>(m_rom.size()),
        checksum,
        readRom32Direct(0),
        readRom32Direct(4),
        m_romLoaded,
    };
}

void MacPlusMachine::incrementLowMemoryTicks()
{
    const auto value = readRam32Direct(lowMemoryTicks) + 1;
    m_ram[lowMemoryTicks] = static_cast<std::uint8_t>(value >> 24);
    m_ram[lowMemoryTicks + 1] = static_cast<std::uint8_t>(value >> 16);
    m_ram[lowMemoryTicks + 2] = static_cast<std::uint8_t>(value >> 8);
    m_ram[lowMemoryTicks + 3] = static_cast<std::uint8_t>(value);
}

std::uint32_t MacPlusMachine::readRam32Direct(std::uint32_t address) const
{
    const auto offset = ramOffset(address);
    return (static_cast<std::uint32_t>(m_ram[offset]) << 24)
        | (static_cast<std::uint32_t>(m_ram[offset + 1]) << 16)
        | (static_cast<std::uint32_t>(m_ram[offset + 2]) << 8)
        | static_cast<std::uint32_t>(m_ram[offset + 3]);
}

void MacPlusMachine::setOverlayEnabled(bool enabled)
{
    if (m_overlayEnabled == enabled) {
        return;
    }

    m_overlayEnabled = enabled;
    logEvent(QStringLiteral("overlay %1").arg(enabled ? QStringLiteral("on") : QStringLiteral("off")));
}

void MacPlusMachine::logEvent(const QString& message)
{
    if (m_eventLog.size() < 256) {
        m_eventLog.append(message);
    }
}

void MacPlusMachine::logAccess(const char* operation, std::uint32_t address, std::uint32_t value)
{
    if (m_eventLog.size() < 256) {
        m_eventLog.append(QStringLiteral("%1 address=0x%2 value=0x%3")
                              .arg(QString::fromLatin1(operation))
                              .arg(address, 6, 16, QLatin1Char('0'))
                              .arg(value, 2, 16, QLatin1Char('0')));
    }
}

void MacPlusMachine::recordBusAccess(const char* operation, Region region, std::uint32_t address, std::uint32_t value, std::uint8_t size)
{
    if (m_busTrace.size() == maxBusTraceEntries) {
        m_busTrace.removeFirst();
    }

    m_busTrace.append({
        QString::fromLatin1(operation),
        regionName(region),
        address,
        value,
        size,
    });
}

void MacPlusMachine::recordSoundBufferWrite(std::uint32_t address, std::uint8_t value)
{
    if (address < soundBase4MiB || address >= soundBase4MiB + soundBytes) {
        return;
    }
    if (m_soundCapture.size() >= maxSoundCaptureBytes) {
        return;
    }

    m_soundCapture.append(static_cast<char>(value));
}

QString MacPlusMachine::regionName(Region region) const
{
    switch (region) {
    case Region::Ram:
        return QStringLiteral("ram");
    case Region::Rom:
        return QStringLiteral("rom");
    case Region::Scc:
        return QStringLiteral("scc");
    case Region::Iwm:
        return QStringLiteral("iwm");
    case Region::Via:
        return QStringLiteral("via");
    case Region::Scsi:
        return QStringLiteral("scsi");
    case Region::Configuration:
        return QStringLiteral("configuration");
    case Region::Unmapped:
        return QStringLiteral("unmapped");
    }

    return QStringLiteral("unknown");
}

std::uint32_t MacPlusMachine::readRom32Direct(std::uint32_t offset) const
{
    offset %= static_cast<std::uint32_t>(m_rom.size());
    return (static_cast<std::uint32_t>(m_rom[offset]) << 24)
        | (static_cast<std::uint32_t>(m_rom[(offset + 1) % m_rom.size()]) << 16)
        | (static_cast<std::uint32_t>(m_rom[(offset + 2) % m_rom.size()]) << 8)
        | static_cast<std::uint32_t>(m_rom[(offset + 3) % m_rom.size()]);
}

} // namespace cutemac::machines::macplus
