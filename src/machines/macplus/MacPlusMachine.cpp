#include "cutemac/machines/macplus/MacPlusMachine.h"
#include "cutemac/rom/RomPatcher.h"

#include <QFile>
#include <QString>

#include <algorithm>
#include <memory>

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
constexpr std::uint32_t lowMemoryMbState = 0x000172;
constexpr std::uint32_t lowMemoryKeyMap = 0x000174;
constexpr std::uint32_t screenBase4MiB = 0x3fa700;
constexpr std::uint32_t lowMemoryMTemp = 0x000828;
constexpr std::uint32_t lowMemoryRawMouse = 0x00082c;
constexpr std::uint32_t lowMemoryMouse = 0x000830;
constexpr std::uint32_t lowMemoryCrsrNew = 0x0008ce;
constexpr std::uint32_t lowMemoryCrsrCouple = 0x0008cf;
constexpr std::uint32_t screenBytes = 512 * 342 / 8;
constexpr std::uint32_t soundBytes = 370;
constexpr std::uint32_t soundBufferTailBytes = 0x300;
constexpr std::uint32_t soundPageDistance = 0x200000;
constexpr int soundSampleRate = 22255;
constexpr std::uint64_t cyclesPerVideoFrame = 130560;
constexpr qsizetype maxPendingAudioBytes = soundSampleRate / 2; // About 250 ms of mono S16.
constexpr std::uint64_t inputDebounceCycles = 130560;
constexpr qsizetype maxBusTraceEntries = 4096;
constexpr qsizetype maxSoundCaptureBytes = 22255 * 30;

constexpr std::uint32_t regionMask = 0xc00000;
constexpr std::uint32_t offset4MiBMask = 0x3fffff;

constexpr std::uint8_t viaOverlayBit = 0x10;
constexpr std::uint8_t viaDiskSelectBit = 0x20;
constexpr std::uint8_t viaSoundPageBit = 0x08;
constexpr std::uint8_t viaSoundEnableBit = 0x80;
constexpr std::uint8_t viaRtcDataBit = 0x01;
constexpr std::uint8_t viaRtcClockBit = 0x02;
constexpr std::uint8_t viaRtcEnableBit = 0x04;

[[nodiscard]] std::uint8_t highByte(std::uint16_t value)
{
    return static_cast<std::uint8_t>(value >> 8);
}

[[nodiscard]] std::uint8_t lowByte(std::uint16_t value)
{
    return static_cast<std::uint8_t>(value);
}

} // namespace

QString MacPlusMachine::machineId() const { return QStringLiteral("mac-plus"); }

MacPlusMachine::MacPlusMachine(std::size_t ramSize, const QString& nvramPath)
    : m_ram(static_cast<qsizetype>(ramSize), 0)
{
    (void)m_rtc.setNvramImagePath(nvramPath);
    m_cpu.setModel(cpu::m68k::M68kCpuCore::Model::M68000);
    m_cpu.setBus(this);

    m_via.setPortAChangedCallback([this](std::uint8_t portA) {
        m_viaPortA = portA;
        m_soundVolume = portA & 0x07;
        setOverlayEnabled((portA & viaOverlayBit) != 0);
        m_iwm.setSideSelect((portA & viaDiskSelectBit) != 0);
    });
    m_via.setPortBChangedCallback([this](std::uint8_t portB, std::uint8_t ddrB) {
        m_soundEnabled = (ddrB & viaSoundEnableBit) != 0 && (portB & viaSoundEnableBit) == 0;
        m_rtc.setPins((portB & viaRtcEnableBit) == 0, (portB & viaRtcClockBit) != 0,
            (portB & viaRtcDataBit) != 0 && (ddrB & viaRtcDataBit) != 0);
        m_via.setPortBInputBit(0, m_rtc.dataLine());
    });
}

bool MacPlusMachine::loadRomFile(const QString& path, const QStringList& enabledPatches)
{
    m_romLoaded = false;
    m_romPath = path;
    m_romSha256.clear();
    m_appliedRomPatches.clear();
    m_romPatchError.clear();

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return false;
    }

    auto data = file.readAll();
    if (data.size() != static_cast<qsizetype>(m_rom.size())) {
        return false;
    }

    const auto patchResult = rom::RomPatcher::apply(data, QStringLiteral("mac-plus"), enabledPatches);
    m_romSha256 = QString::fromLatin1(patchResult.originalSha256.toHex());
    m_romPatchError = patchResult.error;
    if (!patchResult.success) {
        return false;
    }

    std::copy(data.begin(), data.end(), m_rom.begin());
    m_romLoaded = true;
    m_appliedRomPatches = patchResult.appliedPatchIds;
    return true;
}

bool MacPlusMachine::loadDiskImage(const QString& path)
{
    return loadScsiDisk(0, path, false);
}

bool MacPlusMachine::loadScsiDisk(int id, const QString& path, bool readOnly)
{
    if (id < 0 || id >= static_cast<int>(m_scsiDisks.size())) {
        return false;
    }
    auto disk = std::make_shared<devices::scsi::ScsiBlockDevice>();
    if (!disk->loadImage(path, readOnly)) {
        return false;
    }

    m_scsiCdRoms[static_cast<std::size_t>(id)].reset();
    m_scsiDisks[static_cast<std::size_t>(id)] = std::move(disk);
    if (id == 0) m_diskImagePath = path;
    m_scsi.attachTarget(static_cast<std::uint8_t>(id), m_scsiDisks[static_cast<std::size_t>(id)]);
    return true;
}

bool MacPlusMachine::loadScsiCdRom(int id, const QString& path)
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

void MacPlusMachine::ejectScsiCdRom(int id)
{
    if (id < 0 || id >= static_cast<int>(m_scsiCdRoms.size())) return;
    const auto& cdRom = m_scsiCdRoms[static_cast<std::size_t>(id)];
    if (cdRom) cdRom->eject();
}

void MacPlusMachine::ejectDiskImage()
{
    ejectScsiDevice(0);
    m_diskImagePath.clear();
}

void MacPlusMachine::ejectScsiDevice(int id)
{
    if (id < 0 || id >= static_cast<int>(m_scsiDisks.size())) return;
    auto& disk = m_scsiDisks[static_cast<std::size_t>(id)];
    if (disk) disk->eject();
    auto& cdRom = m_scsiCdRoms[static_cast<std::size_t>(id)];
    if (cdRom) cdRom->eject();
    m_scsi.detachTarget(static_cast<std::uint8_t>(id));
    disk.reset();
    cdRom.reset();
    if (id == 0) m_diskImagePath.clear();
}

bool MacPlusMachine::loadFloppyImage(const QString& path, bool readOnly)
{
    return m_iwm.loadFloppyImage(path, readOnly);
}

void MacPlusMachine::ejectFloppyImage()
{
    m_iwm.ejectFloppyImage();
}

void MacPlusMachine::reset()
{
    m_accessSummary = {};
    m_eventLog.clear();
    m_soundCapture.clear();
    m_pendingAudio.clear();
    m_audioCyclePhase = 0;
    m_audioBufferIndex = 0;

    std::fill(m_ram.begin(), m_ram.end(), 0);
    m_scc.reset();
    m_iwm.reset();
    m_scsi.reset();
    for (std::size_t id = 0; id < m_scsiDisks.size(); ++id) {
        if (m_scsiDisks[id] && m_scsiDisks[id]->ready()) {
            m_scsi.attachTarget(static_cast<std::uint8_t>(id), m_scsiDisks[id]);
        } else if (m_scsiCdRoms[id]) {
            m_scsi.attachTarget(static_cast<std::uint8_t>(id), m_scsiCdRoms[id]);
        }
    }
    m_rtc.resetSerial();
    m_via.reset();
    m_scheduler.reset();
    m_lastMouseButtonCycle = 0;
    m_queuedMouseButtonPressed = false;
    setOverlayEnabled(true);

    m_cpu.reset();
    synchronizeMouseLowMemory();
    logEvent(QStringLiteral("reset pc=0x%1 overlay=%2")
                 .arg(programCounter(), 8, 16, QLatin1Char('0'))
                 .arg(overlayEnabled() ? QStringLiteral("on") : QStringLiteral("off")));
}

int MacPlusMachine::runCycles(int cycles)
{
    int cyclesRun = 0;
    while (cyclesRun < cycles) {
        cyclesRun += std::max(1, stepInstruction());
    }
    return cyclesRun;
}

int MacPlusMachine::stepInstruction()
{
    m_scheduler.dispatchDue();
    updateInterrupts();
    const auto cycles = m_cpu.stepInstruction();
    const auto elapsed = std::max(1, cycles);
    m_via.tick(elapsed);
    advanceAudio(elapsed);
    m_scheduler.advance(static_cast<std::uint64_t>(elapsed));
    updateInterrupts();
    return cycles;
}

QString MacPlusMachine::debugCpuArchitecture() const
{
    return QStringLiteral("m68k");
}

QStringList MacPlusMachine::debugRegisterLines() const
{
    const auto regs = cpuRegisters();
    QStringList lines;
    for (int first = 0; first < 8; first += 4) {
        QString line;
        for (int i = first; i < first + 4; ++i)
            line += QStringLiteral("D%1=0x%2%3").arg(i).arg(regs.d[i], 8, 16, QLatin1Char('0')).arg(i == first + 3 ? QString() : QStringLiteral(" "));
        lines.append(line);
    }
    for (int first = 0; first < 8; first += 4) {
        QString line;
        for (int i = first; i < first + 4; ++i)
            line += QStringLiteral("A%1=0x%2%3").arg(i).arg(regs.a[i], 8, 16, QLatin1Char('0')).arg(i == first + 3 ? QString() : QStringLiteral(" "));
        lines.append(line);
    }
    lines.append(QStringLiteral("PC=0x%1 SR=0x%2 USP=0x%3 ISP=0x%4 MSP=0x%5 VBR=0x%6")
        .arg(regs.pc, 8, 16, QLatin1Char('0')).arg(regs.sr, 4, 16, QLatin1Char('0'))
        .arg(regs.usp, 8, 16, QLatin1Char('0')).arg(regs.isp, 8, 16, QLatin1Char('0'))
        .arg(regs.msp, 8, 16, QLatin1Char('0')).arg(regs.vbr, 8, 16, QLatin1Char('0')));
    return lines;
}

std::uint64_t MacPlusMachine::cycleCount() const { return m_scheduler.now(); }

void MacPlusMachine::queueInput(const core::GuestInputEvent& event, std::uint64_t cycle)
{
    auto deliveryCycle = std::max(cycle, m_scheduler.now());
    if (event.type == core::GuestInputEvent::Type::MouseButton) {
        if (event.pressed == m_queuedMouseButtonPressed) {
            return;
        }
        m_queuedMouseButtonPressed = event.pressed;
        deliveryCycle = std::max(deliveryCycle, m_lastMouseButtonCycle + inputDebounceCycles);
        m_lastMouseButtonCycle = deliveryCycle;
    }
    m_scheduler.schedule(deliveryCycle, [this, event]() { applyInput(event); });
}

void MacPlusMachine::applyInput(const core::GuestInputEvent& event)
{
    switch (event.type) {
    case core::GuestInputEvent::Type::MousePosition:
        setMousePosition(static_cast<std::int16_t>(event.first), static_cast<std::int16_t>(event.second));
        break;
    case core::GuestInputEvent::Type::MouseDelta:
        moveMouse(static_cast<std::int16_t>(event.first), static_cast<std::int16_t>(event.second));
        break;
    case core::GuestInputEvent::Type::MouseButton:
        setMouseButton(event.pressed);
        break;
    case core::GuestInputEvent::Type::Key:
        setKeyState(static_cast<std::uint8_t>(event.first), event.pressed);
        break;
    case core::GuestInputEvent::Type::ResetKeyboard:
        resetKeyboard();
        break;
    }
}

bool MacPlusMachine::runUntilPc(std::uint32_t address, int maxCycles)
{
    int cyclesUsed = 0;
    while (cyclesUsed < maxCycles) {
        if (programCounter() == (address & 0x00ffffff)) {
            return true;
        }
        cyclesUsed += std::max(1, stepInstruction());
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

void MacPlusMachine::setBusTraceEnabled(bool enabled)
{
    m_busTraceEnabled = enabled;
    if (!enabled) {
        m_busTrace.clear();
    }
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
        const auto offset = (address - romBase) & offset4MiBMask;
        const auto value = offset < m_rom.size()
            ? m_rom[romOffset(address)]
            : static_cast<std::uint8_t>(0x5a ^ (offset >> 9) ^ (offset >> 17));
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
        (void)m_iwm.access(registerIndex, value, true);
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
        const auto offset = (address - romBase) & offset4MiBMask;
        if (offset < m_rom.size()) {
            return m_rom[romOffset(address)];
        }
        return static_cast<std::uint8_t>(0x5a ^ (offset >> 9) ^ (offset >> 17));
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

devices::video::VideoFrame MacPlusMachine::videoFrame() const
{
    return {
        512,
        342,
        512 / 8,
        devices::video::PixelStorage::Indexed,
        1,
        devices::video::ByteOrder::BigEndian,
        devices::video::BitOrder::MostSignificantFirst,
        framebufferBytes(),
        { 0xffffffffU, 0xff000000U },
        { 0, 1 },
        {},
    };
}

devices::audio::AudioFrame MacPlusMachine::takeAudioFrame()
{
    devices::audio::AudioFrame frame { soundSampleRate, 1,
        devices::audio::SampleFormat::SignedInt16, std::move(m_pendingAudio) };
    m_pendingAudio.clear();
    return frame;
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
    const auto base = soundBufferBase(true);
    for (std::uint32_t i = 0; i < soundBytes; ++i) {
        bytes[static_cast<qsizetype>(i)] = static_cast<char>(debugRead8(base + i));
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

void MacPlusMachine::setSoundCaptureEnabled(bool enabled)
{
    m_soundCaptureEnabled = enabled;
    if (!enabled) {
        m_soundCapture.clear();
    }
}

QString MacPlusMachine::diskImagePath() const
{
    return m_diskImagePath;
}

QString MacPlusMachine::floppyImagePath() const
{
    return m_iwm.floppyImagePath();
}

devices::scsi::ncr5380::Ncr5380::DebugState MacPlusMachine::scsiDebugState() const
{
    return m_scsi.debugState();
}

devices::iwm::IwmController::DebugState MacPlusMachine::iwmDebugState() const
{
    return m_iwm.debugState();
}

QByteArray MacPlusMachine::floppyTrackBytesForDebug(int track, int side) const
{
    return m_iwm.trackBytesForDebug(track, side);
}

void MacPlusMachine::setIwmTraceEnabled(bool enabled)
{
    m_iwm.setTraceEnabled(enabled);
}

void MacPlusMachine::clearIwmTrace()
{
    m_iwm.clearTrace();
}

QStringList MacPlusMachine::iwmTraceEvents() const
{
    return m_iwm.traceEvents();
}

QByteArray MacPlusMachine::iwmLastNibblesForDebug() const
{
    return m_iwm.lastNibblesForDebug();
}

devices::via6522::Via6522::DebugState MacPlusMachine::viaDebugState() const
{
    return m_via.debugState();
}

void MacPlusMachine::setMousePosition(std::int16_t x, std::int16_t y)
{
    m_mouseX = std::clamp<std::int16_t>(x, 0, 511);
    m_mouseY = std::clamp<std::int16_t>(y, 0, 341);
    const auto packed = (static_cast<std::uint32_t>(static_cast<std::uint16_t>(m_mouseY)) << 16)
        | static_cast<std::uint16_t>(m_mouseX);
    writeRam32Direct(lowMemoryMTemp, packed);
    writeRam8Direct(lowMemoryCrsrCouple, 0xff);
    writeRam8Direct(lowMemoryCrsrNew, 0xff);
}

void MacPlusMachine::moveMouse(std::int16_t dx, std::int16_t dy)
{
    const auto currentY = static_cast<std::int16_t>(readRam16Direct(lowMemoryMTemp));
    const auto currentX = static_cast<std::int16_t>(readRam16Direct(lowMemoryMTemp + 2));
    setMousePosition(static_cast<std::int16_t>(currentX + dx), static_cast<std::int16_t>(currentY + dy));
}

void MacPlusMachine::setMouseButton(bool pressed)
{
    m_mouseButtonPressed = pressed;
    m_via.setPortBInputBit(3, !pressed);
}

void MacPlusMachine::setKeyState(std::uint8_t macKeyCode, bool pressed)
{
    m_via.queueKeyboardTransition(macKeyCode, pressed);
}

void MacPlusMachine::resetKeyboard()
{
    for (std::uint32_t i = 0; i < 18; ++i) {
        writeRam8Direct(lowMemoryKeyMap + i, 0);
    }
}

std::int16_t MacPlusMachine::mouseX() const
{
    return m_mouseX;
}

std::int16_t MacPlusMachine::mouseY() const
{
    return m_mouseY;
}

bool MacPlusMachine::mouseButtonPressed() const
{
    return m_mouseButtonPressed;
}

QByteArray MacPlusMachine::keyMapBytes() const
{
    QByteArray bytes;
    bytes.resize(18);
    for (std::uint32_t i = 0; i < 18; ++i) {
        bytes[static_cast<qsizetype>(i)] = static_cast<char>(readRam8Direct(lowMemoryKeyMap + i));
    }
    return bytes;
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
        m_romSha256,
        m_appliedRomPatches,
        m_romPatchError,
        m_romLoaded,
    };
}

void MacPlusMachine::updateInterrupts()
{
    m_cpu.setIrqLevel(m_via.interruptActive() ? 1 : 0);
}

std::uint32_t MacPlusMachine::readRam32Direct(std::uint32_t address) const
{
    const auto offset = ramOffset(address);
    return (static_cast<std::uint32_t>(m_ram[offset]) << 24)
        | (static_cast<std::uint32_t>(m_ram[offset + 1]) << 16)
        | (static_cast<std::uint32_t>(m_ram[offset + 2]) << 8)
        | static_cast<std::uint32_t>(m_ram[offset + 3]);
}

std::uint16_t MacPlusMachine::readRam16Direct(std::uint32_t address) const
{
    const auto offset = ramOffset(address);
    return static_cast<std::uint16_t>((m_ram[offset] << 8) | m_ram[offset + 1]);
}

std::uint8_t MacPlusMachine::readRam8Direct(std::uint32_t address) const
{
    return m_ram[ramOffset(address)];
}

void MacPlusMachine::writeRam8Direct(std::uint32_t address, std::uint8_t value)
{
    if (address < static_cast<std::uint32_t>(m_ram.size())) {
        m_ram[ramOffset(address)] = value;
    }
}

void MacPlusMachine::writeRam16Direct(std::uint32_t address, std::uint16_t value)
{
    writeRam8Direct(address, highByte(value));
    writeRam8Direct(address + 1, lowByte(value));
}

void MacPlusMachine::writeRam32Direct(std::uint32_t address, std::uint32_t value)
{
    writeRam16Direct(address, static_cast<std::uint16_t>(value >> 16));
    writeRam16Direct(address + 2, static_cast<std::uint16_t>(value));
}

void MacPlusMachine::synchronizeMouseLowMemory()
{
    const auto packed = (static_cast<std::uint32_t>(static_cast<std::uint16_t>(m_mouseY)) << 16)
        | static_cast<std::uint16_t>(m_mouseX);
    writeRam32Direct(lowMemoryMTemp, packed);
    writeRam32Direct(lowMemoryRawMouse, packed);
    writeRam32Direct(lowMemoryMouse, packed);
    writeRam8Direct(lowMemoryCrsrCouple, 0xff);
    writeRam8Direct(lowMemoryCrsrNew, 0xff);
    m_via.setPortBInputBit(3, !m_mouseButtonPressed);
    writeRam8Direct(lowMemoryMbState, m_mouseButtonPressed ? 0x00 : 0x80);
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
    if (!m_busTraceEnabled) {
        return;
    }
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
    if (!m_soundCaptureEnabled) {
        return;
    }
    const auto base = soundBufferBase(true);
    if (address < base || address >= base + soundBytes) {
        return;
    }
    if (m_soundCapture.size() >= maxSoundCaptureBytes) {
        return;
    }

    m_soundCapture.append(static_cast<char>(value));
}

void MacPlusMachine::advanceAudio(int cycles)
{
    m_audioCyclePhase += static_cast<std::uint64_t>(cycles) * soundBytes;
    while (m_audioCyclePhase >= cyclesPerVideoFrame) {
        m_audioCyclePhase -= cyclesPerVideoFrame;

        std::int16_t sample = 0;
        if (m_soundEnabled && m_soundVolume != 0) {
            const auto base = soundBufferBase((m_viaPortA & viaSoundPageBit) != 0);
            const auto source = static_cast<int>(debugRead8(base + m_audioBufferIndex)) - 128;
            sample = static_cast<std::int16_t>(source * 256 * m_soundVolume / 7);
        }
        m_pendingAudio.append(reinterpret_cast<const char*>(&sample), sizeof(sample));
        m_audioBufferIndex = (m_audioBufferIndex + 1) % soundBytes;
    }

    if (m_pendingAudio.size() > maxPendingAudioBytes) {
        m_pendingAudio.remove(0, m_pendingAudio.size() - maxPendingAudioBytes);
    }
}

std::uint32_t MacPlusMachine::soundBufferBase(bool mainPage) const
{
    const auto mainBase = static_cast<std::uint32_t>(m_ram.size()) - soundBufferTailBytes;
    return !mainPage && mainBase >= soundPageDistance ? mainBase - soundPageDistance : mainBase;
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
