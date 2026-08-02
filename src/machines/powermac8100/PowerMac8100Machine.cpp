#include "cutemac/machines/powermac8100/PowerMac8100Machine.h"

#include <algorithm>

#include <QFile>

#include "cutemac/rom/RomPatcher.h"
#include "cutemac/devices/video/nubus/CuteMacVideoCard.h"
#include "cutemac/devices/video/nubus/MacintoshIIVideoCard.h"

namespace cutemac::machines::powermac8100 {
namespace {
constexpr std::uint32_t romBase = 0x40000000U;
constexpr std::uint32_t resetRomBase = 0xffc00000U;
constexpr std::uint32_t romSize = 4U * 1024U * 1024U;
constexpr std::uint32_t amicBase = 0x50f00000U;
constexpr std::uint32_t hmcBase = 0x50f40000U;
constexpr std::uint32_t machineIdAddress = 0x5ffffffcU;
constexpr std::uint32_t machineIdValue = 0xa55a3013U;
constexpr std::uint32_t motherboardRamSize = 8U * 1024U * 1024U;
constexpr std::size_t maxBusTrace = 1'000'000;
}

PowerMac8100Machine::PowerMac8100Machine(std::size_t ramSize)
    : m_ram(static_cast<qsizetype>(std::max<std::size_t>(ramSize, 8U * 1024U * 1024U)), 0)
    , m_videoRam(2 * 1024 * 1024, 0)
    , m_amicRegisters(0x40000, 0)
{
    m_cpu.setBus(this);
    m_cpu.setTraceSink([this](const cpu::ppc::PowerPc601Core::TraceEvent& event) {
        if (!m_cpuTraceEnabled || m_cpuTraceFrozen) return;
        if (event.kind == cpu::ppc::PowerPc601Core::TraceEvent::Kind::Translation) return;
        constexpr std::size_t maxCpuTrace = 65'536;
        if (m_cpuTrace.size() == maxCpuTrace) m_cpuTrace.pop_front();
        m_cpuTrace.push_back(event);
        if (m_freezeCpuTraceOnEmulatorException
            && event.kind == cpu::ppc::PowerPc601Core::TraceEvent::Kind::Exception
            && event.pc >= 0x68000000U && event.pc < 0x68100000U)
            m_cpuTraceFrozen = true;
    });
    m_cpu.setClockFrequency(80'000'000U);
    m_via1.setPowerOnState(0, 0, 0, 0);
    m_via1.setAutomaticCa1Period(1'330'008);
    m_cuda.attach(&m_via1);
    m_via1.setPortBChangedCallback([this](std::uint8_t output, std::uint8_t direction) {
        m_cuda.portBChanged(output, direction);
    });
    m_via1.setShiftRegisterWriteCallback([this](std::uint8_t value) { m_cuda.shiftByteFromHost(value); });
    m_via1.setShiftRegisterReadCallback([this] { m_cuda.shiftByteToHostConsumed(); });
    // The 8100 exposes NuBus slots C-E. Keep the bring-up display in slot E,
    // matching a physical slot which the unmodified ROM enumerates.
    (void)m_nubus.install(14, std::make_shared<devices::video::nubus::CuteMacVideoCard>(640, 480, 8, 4, false));
}

QString PowerMac8100Machine::machineId() const { return QStringLiteral("powermac-8100"); }

void PowerMac8100Machine::attachSerialEndpoint(int channel, std::shared_ptr<devices::serial::SerialEndpoint> endpoint)
{
    m_scc.attachEndpoint(channel == 0 ? devices::scc::Z8530Scc::Channel::A : devices::scc::Z8530Scc::Channel::B, std::move(endpoint));
}

bool PowerMac8100Machine::loadScsiDisk(int id, const QString& path, bool readOnly)
{
    if (id < 0 || id >= static_cast<int>(m_scsiDisks.size())) return false;
    auto disk = std::make_shared<devices::scsi::ScsiBlockDevice>();
    if (!disk->loadImage(path, readOnly)) return false;
    m_scsiDisks[static_cast<std::size_t>(id)] = disk;
    // The 8100's configured fixed disks live on its internal Fast SCSI bus.
    m_scsiB.attachTarget(static_cast<std::uint8_t>(id), std::move(disk));
    return true;
}

void PowerMac8100Machine::ejectScsiDevice(int id)
{
    if (id < 0 || id >= static_cast<int>(m_scsiDisks.size())) return;
    m_scsiDisks[static_cast<std::size_t>(id)].reset();
    m_scsiB.detachTarget(static_cast<std::uint8_t>(id));
}

bool PowerMac8100Machine::loadRomFile(const QString& path, const QStringList& patches)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return false;
    auto bytes = file.readAll();
    if (bytes.size() != romSize) return false;
    const auto patched = rom::RomPatcher::apply(bytes, machineId(), patches);
    if (!patched.success) return false;
    m_rom = std::move(bytes);
    m_romLoaded = true;
    return true;
}

void PowerMac8100Machine::reset()
{
    std::fill(m_videoRam.begin(), m_videoRam.end(), 0);
    std::fill(m_ram.begin(), m_ram.end(), 0);
    m_scheduler.reset();
    m_scc.reset();
    m_scsi.reset();
    m_scsiB.reset();
    m_via1.reset();
    for (std::uint8_t bit = 0; bit < 8; ++bit) {
        m_via1.setPortAInputBit(bit, false);
        m_via1.setPortBInputBit(bit, false);
    }
    m_via1.setPortBInputBit(3, true); // Cuda TREQ is inactive high after reset.
    m_cuda.reset();
    m_video.reset();
    m_nubus.reset();
    m_hmcControl = 0;
    m_hmcShiftBuffer = 0;
    m_hmcBitPosition = 0;
    m_hmcCommitted = false;
    m_compatibilityRamActive = false;
    m_via2Ier = m_via2Ifr = m_via2SlotIer = 0;
    m_via2SlotIfr = 0x7f;
    m_irqControl = 0;
    m_irqLineAsserted = false;
    m_lastInterruptAssertState.fill(0);
    m_dmaBase = m_scsiDmaAddress = m_scsiDmaOffset = 0;
    m_scsiDmaControl = 0;
    m_soundDmaStartCycle = 0;
    m_pendingDeviceCycles = 0;
    m_videoVblankCycles = 1'330'008;
    m_amicRegisters.fill(0);
    m_unmappedAccessCount = 0;
    m_ascCompatibilityReadCounts.fill(0);
    m_busTrace.clear();
    m_cpuTrace.clear();
    m_cpuTraceFrozen = false;
    m_cpu.reset();
}

int PowerMac8100Machine::runCycles(int cycles)
{
    int used = 0;
    while (used < cycles) used += stepInstruction();
    return used;
}

int PowerMac8100Machine::stepInstruction()
{
    const auto cycles = std::max(1, m_cpu.stepInstruction());
    m_cpu.advanceTime(static_cast<std::uint32_t>(cycles));
    m_pendingDeviceCycles += cycles;
    if (m_pendingDeviceCycles >= 64) flushDevices();
    return cycles;
}

void PowerMac8100Machine::flushDevices()
{
    if (m_pendingDeviceCycles == 0) return;
    m_scc.tick(m_pendingDeviceCycles);
    m_nubus.tick(static_cast<std::uint64_t>(m_pendingDeviceCycles));
    m_via1.tick(m_pendingDeviceCycles);
    m_cuda.tick(m_pendingDeviceCycles);
    // Sonora scan timing continues while its output is blanked and while the
    // mode registers are being programmed.  The ROM acknowledges slot bit 6
    // and waits for the following edge before it enables the framebuffer.
    m_videoVblankCycles -= m_pendingDeviceCycles;
    if (m_videoVblankCycles <= 0) {
        do m_videoVblankCycles += 1'330'008; while (m_videoVblankCycles <= 0);
        m_via2SlotIfr &= static_cast<std::uint8_t>(~0x40U);
        if ((~m_via2SlotIfr) & m_via2SlotIer) m_via2Ifr |= 0x02U;
    }
    m_scheduler.advance(static_cast<std::uint64_t>(m_pendingDeviceCycles));
    m_pendingDeviceCycles = 0;
    updateInterrupts();
}

std::uint64_t PowerMac8100Machine::cycleCount() const { return m_scheduler.now() + static_cast<std::uint64_t>(m_pendingDeviceCycles); }
std::uint32_t PowerMac8100Machine::programCounter() const { return m_cpu.programCounter(); }
QByteArray PowerMac8100Machine::framebufferBytes() const { return videoFrame().pixels; }
devices::video::VideoFrame PowerMac8100Machine::videoFrame() const
{
    // The 8100's built-in Sonora display is the primary framebuffer once the
    // ROM enables it.  A reset-valid NuBus card must not permanently mask it.
    const auto onboard = m_video.frame(m_videoRam);
    if (onboard.valid()) return onboard;
    for (int slot = 9; slot <= 14; ++slot) {
        const auto card = m_nubus.card(slot);
        if (card) {
            const auto frame = card->videoFrame();
            if (frame.valid()) return frame;
        }
    }
    return {};
}

void PowerMac8100Machine::updateInterrupts()
{
    // AMIC presents the 53C94 DRQ as VIA2 IFR bit 0.  The ROM polls this
    // bit even when it subsequently uses the AMIC DMA engine, so exposing
    // only the controller's interrupt bit leaves SCSI transfers stalled.
    if (m_scsi.dmaRequest() || m_scsiB.dmaRequest()) m_via2Ifr |= 0x01U; else m_via2Ifr &= static_cast<std::uint8_t>(~0x01U);
    if (m_scsi.interruptActive() || m_scsiB.interruptActive()) m_via2Ifr |= 0x08U;
    else m_via2Ifr &= static_cast<std::uint8_t>(~0x08U);
    if ((~m_via2SlotIfr) & m_via2SlotIer) m_via2Ifr |= 0x02U; else m_via2Ifr &= static_cast<std::uint8_t>(~0x02U);
    const auto sources = static_cast<std::uint8_t>((m_via1.interruptActive() ? 1U : 0U)
        | ((m_via2Ifr & m_via2Ier) ? 2U : 0U));
    const auto oldSources = static_cast<std::uint8_t>(m_irqControl & 0x3fU);
    m_irqControl = static_cast<std::uint8_t>((m_irqControl & 0xc0U) | sources);
    if (m_irqControl & 0x40U) {
        // In 68k-compatible mode AMIC latches any source transition until
        // software acknowledges bit 7 through the interrupt-control port.
        if (oldSources != sources) m_irqControl |= 0x80U;
    } else if (sources) {
        m_irqControl |= 0x80U;
    } else {
        m_irqControl &= 0x7fU;
    }
    const bool asserted = (m_irqControl & 0x80U) != 0;
    if (asserted && !m_irqLineAsserted) {
        m_lastInterruptAssertState = { m_irqControl, oldSources, m_via2Ifr, m_via2Ier,
            m_via2SlotIfr, m_via2SlotIer };
    }
    m_irqLineAsserted = asserted;
    // Bit 7 records a source transition in 68k-compatible mode, including a
    // falling edge after the VIA flag has been consumed by polled code.  Keep
    // that latch visible to the ROM, but do not turn a stale, source-less
    // record into a live PPC exception: the reconstructed level-1 handler
    // would find no VIA flag and enter the System Error monitor.
    m_cpu.setExternalInterrupt(asserted);
}

void PowerMac8100Machine::serviceScsiDma()
{
    if (!(m_scsiDmaControl & 0x02U)) return;
    unsigned guard = 0;
    while (m_scsi.dmaRequest() && guard++ < 1'048'576U) {
        const auto address = m_scsiDmaAddress + m_scsiDmaOffset;
        if (m_scsi.dmaToHost()) {
            const auto swapped = m_scsi.readDmaWord();
            const auto word = static_cast<std::uint16_t>((swapped << 8) | (swapped >> 8));
            writeMapped8(address, static_cast<std::uint8_t>(word >> 8));
            writeMapped8(address + 1, static_cast<std::uint8_t>(word));
        } else {
            const auto word = static_cast<std::uint16_t>((readMapped8(address) << 8) | readMapped8(address + 1));
            m_scsi.writeDmaWord(static_cast<std::uint16_t>((word << 8) | (word >> 8)));
        }
        m_scsiDmaOffset += 2;
    }
    updateInterrupts();
}

PowerMac8100Machine::BusRegion PowerMac8100Machine::regionFor(std::uint32_t address) const
{
    if (ramIndex(address)) return BusRegion::Ram;
    if ((address >= romBase && address < 0x50000000U)
        || address >= resetRomBase) return BusRegion::Rom;
    if (address >= hmcBase && address < hmcBase + 0x10000U) return BusRegion::Hmc;
    if (address >= amicBase && address < amicBase + 0x40000U) return BusRegion::Amic;
    if (address >= machineIdAddress && address < machineIdAddress + 4U) return BusRegion::MachineId;
    // PDM forwards the complete 0x60000000-0xefffffff super-slot aperture to
    // NuBus.  Only the canonical slot windows select an installed card, but
    // probes elsewhere in the aperture still complete as open-bus cycles.
    if (devices::nubus::NuBusBus::standardSlot(address) >= 0
        || (address >= 0x60000000U && address < 0xf0000000U)) return BusRegion::NuBus;
    return BusRegion::Unmapped;
}

std::optional<std::size_t> PowerMac8100Machine::ramIndex(std::uint32_t address) const
{
    const auto total = static_cast<std::size_t>(m_ram.size());
    if (!m_hmcCommitted) return address < total ? std::optional<std::size_t>(address) : std::nullopt;
    if (address < motherboardRamSize) return address;
    if (total <= motherboardRamSize) return std::nullopt;

    const auto simmSize = (total - motherboardRamSize) / 2U;
    if (simmSize == 0) return std::nullopt;
    if (address >= motherboardRamSize && address < motherboardRamSize + simmSize)
        return motherboardRamSize + (address - motherboardRamSize);
    if (address >= 0x10000000U && address < 0x10000000U + simmSize)
        return motherboardRamSize + (address - 0x10000000U);

    static constexpr std::uint32_t configuredBankSizes[] {
        128U * 1024U * 1024U, 2U * 1024U * 1024U, 8U * 1024U * 1024U, 32U * 1024U * 1024U
    };
    const auto configuration = static_cast<unsigned>((m_hmcControl >> 29) & 3U);
    const auto bankBBase = configuration == 0 ? 0x08000000U
        : motherboardRamSize + configuredBankSizes[configuration];
    if (address >= bankBBase && address < bankBBase + simmSize)
        return motherboardRamSize + simmSize + (address - bankBBase);
    if (simmSize < motherboardRamSize) {
        const auto alias = 0x10000000U + static_cast<std::uint32_t>(simmSize) - motherboardRamSize;
        if (address >= alias && address < alias + simmSize)
            return motherboardRamSize + simmSize + (address - alias);
    }
    return std::nullopt;
}

std::uint8_t PowerMac8100Machine::readHmc(std::uint32_t address)
{
    const auto offset = address - hmcBase;
    if (offset == 0) {
        const auto value = static_cast<std::uint8_t>((m_hmcControl >> m_hmcBitPosition) & 1U);
        m_hmcBitPosition = (m_hmcBitPosition + 1U) % 35U;
        return value;
    }
    return 0;
}

void PowerMac8100Machine::writeHmc(std::uint32_t address, std::uint8_t value)
{
    const auto offset = address - hmcBase;
    if (offset == 0) {
        const auto bit = std::uint64_t { 1 } << m_hmcBitPosition;
        m_hmcShiftBuffer = (value & 1U) ? m_hmcShiftBuffer | bit : m_hmcShiftBuffer & ~bit;
        ++m_hmcBitPosition;
        if (m_hmcBitPosition == 35U) {
            m_hmcControl = m_hmcShiftBuffer & ~std::uint64_t { 3 };
            m_video.setVramOffset((m_hmcControl & 0x200000000ULL) ? 0U : 0x100000U);
            m_hmcBitPosition = 0;
            m_hmcCommitted = true;
        }
    } else if (offset == 8) {
        m_hmcBitPosition = 0;
    }
}

std::uint8_t PowerMac8100Machine::readMapped8(std::uint32_t address)
{
    switch (regionFor(address)) {
    case BusRegion::Ram: return m_ram[static_cast<qsizetype>(*ramIndex(address))];
    case BusRegion::Rom: return m_romLoaded ? static_cast<std::uint8_t>(m_rom.at((address - romBase) & (romSize - 1U))) : 0xff;
    case BusRegion::Hmc: return readHmc(address);
    case BusRegion::Amic: {
        flushDevices();
        const auto offset = address - amicBase;
        if (offset >= 0x10000U && offset < 0x10100U)
            return m_scsi.readRegister(static_cast<std::uint8_t>((offset & 0xffU) >> 4));
        if (offset >= 0x11000U && offset < 0x11100U)
            return m_scsiB.readRegister(static_cast<std::uint8_t>((offset & 0xffU) >> 4));
        if (offset >= 0x31000U && offset < 0x31004U)
            return static_cast<std::uint8_t>(m_dmaBase >> ((3U - (offset & 3U)) * 8U));
        if (offset >= 0x32000U && offset < 0x32004U)
            return static_cast<std::uint8_t>(m_scsiDmaAddress >> ((3U - (offset & 3U)) * 8U));
        if (offset == 0x32008U) return m_scsiDmaControl;
        if (offset >= 0x32010U && offset < 0x32014U) {
            const auto current = m_scsiDmaAddress + m_scsiDmaOffset;
            return static_cast<std::uint8_t>(current >> ((3U - (offset & 3U)) * 8U));
        }
        if (offset < 0x2000U) return m_via1.readRegister(static_cast<std::uint8_t>((offset >> 9) & 15U));
        if (offset >= 0x24000U && offset < 0x24004U) return m_video.readDac(offset - 0x24000U);
        if (offset >= 0x28000U && offset < 0x28008U) return m_video.readControl(offset - 0x28000U);
        if ((offset & 0x3ffe0U) == 0x26000U) {
            switch (offset & 0x1fU) {
            case 2: return m_via2SlotIfr;
            case 3: return m_via2Ifr;
            case 0x12: return m_via2SlotIer;
            case 0x13: return m_via2Ier;
            default: return 0;
            }
        }
        if (offset >= 0x2a000U && offset < 0x2a010U) {
            if ((offset & 15U) == 0) return m_irqControl;
            return 0;
        }
        if (offset >= 0x4000U && offset < 0x400cU) {
            using Channel = devices::scc::Z8530Scc::Channel;
            switch (offset & 6U) {
            case 6: return m_scc.readData(Channel::A);
            case 4: return m_scc.readData(Channel::B);
            case 2: return m_scc.readControl(Channel::A);
            default: return m_scc.readControl(Channel::B);
            }
        }
        if (offset >= 0x2c000U && offset < 0x2e000U)
            // AMIC diagnostic aperture: bit 0 at the base is the inactive
            // EMMO/factory-test pin.  Other offsets read as zero.  Returning
            // all ones makes the ROM enter its post-bong diagnostic monitor.
            return offset == 0x2c000U ? 1U : 0U;
        if (offset >= 0x1400cU && offset <= 0x1400eU) return 0; // idle sound phase
        if (offset == 0x14018U && (static_cast<std::uint8_t>(m_amicRegisters[0x14010]) & 1U)
            && m_scheduler.now() - m_soundDmaStartCycle >= 4096U) {
            m_amicRegisters[0x14018] = static_cast<char>(static_cast<std::uint8_t>(m_amicRegisters[0x14018]) | 0xc0U);
        }
        return static_cast<std::uint8_t>(m_amicRegisters[static_cast<qsizetype>(offset)]);
    }
    case BusRegion::NuBus: return m_nubus.read8(address);
    case BusRegion::MachineId: return static_cast<std::uint8_t>(machineIdValue >> ((3U - (address & 3U)) * 8U));
    case BusRegion::Unmapped: ++m_unmappedAccessCount; return 0xff;
    }
    return 0xff;
}

void PowerMac8100Machine::writeMapped8(std::uint32_t address, std::uint8_t value)
{
    switch (regionFor(address)) {
    case BusRegion::Ram: m_ram[static_cast<qsizetype>(*ramIndex(address))] = value; break;
    case BusRegion::Hmc: writeHmc(address, value); break;
    case BusRegion::Amic: {
        flushDevices();
        const auto offset = address - amicBase;
        if (offset >= 0x10000U && offset < 0x10100U) {
            m_scsi.writeRegister(static_cast<std::uint8_t>((offset & 0xffU) >> 4), value); serviceScsiDma(); updateInterrupts(); break;
        }
        if (offset >= 0x11000U && offset < 0x11100U) {
            m_scsiB.writeRegister(static_cast<std::uint8_t>((offset & 0xffU) >> 4), value); updateInterrupts(); break;
        }
        if (offset >= 0x31000U && offset < 0x31004U) {
            const auto shift = (3U - (offset & 3U)) * 8U;
            m_dmaBase = (m_dmaBase & ~(0xffU << shift)) | (static_cast<std::uint32_t>(value) << shift);
            m_dmaBase &= 0xfffc0000U; break;
        }
        if (offset >= 0x32000U && offset < 0x32004U) {
            const auto shift = (3U - (offset & 3U)) * 8U;
            m_scsiDmaAddress = (m_scsiDmaAddress & ~(0xffU << shift)) | (static_cast<std::uint32_t>(value) << shift);
            m_scsiDmaAddress &= ~7U; m_scsiDmaOffset = 0; break;
        }
        if (offset == 0x32008U) {
            m_scsiDmaControl = value & 0x4eU;
            if (value & 1U) { m_scsiDmaControl &= 0x40U; m_scsiDmaOffset = 0; }
            serviceScsiDma(); break;
        }
        if (offset < 0x2000U) { m_via1.writeRegister(static_cast<std::uint8_t>((offset >> 9) & 15U), value); updateInterrupts(); break; }
        if (offset >= 0x24000U && offset < 0x24004U) { m_video.writeDac(offset - 0x24000U, value); break; }
        if (offset >= 0x28000U && offset < 0x28008U) { m_video.writeControl(offset - 0x28000U, value); break; }
        if ((offset & 0x3ffe0U) == 0x26000U) {
            switch (offset & 0x1fU) {
            case 2:
                if (value & static_cast<std::uint8_t>(~m_via2SlotIfr) & 0x40U) m_via2SlotIfr |= 0x40U;
                break;
            case 0x12:
                if (value & 0x80U) m_via2SlotIer |= value & 0x78U; else m_via2SlotIer &= static_cast<std::uint8_t>(~value);
                break;
            case 0x13:
                if (value & 0x80U) m_via2Ier |= value & 0x3bU; else m_via2Ier &= static_cast<std::uint8_t>(~value);
                break;
            default: break;
            }
            updateInterrupts();
            break;
        }
        if (offset >= 0x2a000U && offset < 0x2a010U) {
            if ((offset & 15U) == 0) {
                if ((m_irqControl ^ value) & 0x40U) m_irqControl = static_cast<std::uint8_t>((m_irqControl & ~0xc0U) | (value & 0x40U));
                if ((value & 0xc0U) == 0xc0U) m_irqControl &= 0x7fU;
                updateInterrupts();
            }
            break;
        }
        if (offset >= 0x4000U && offset < 0x400cU) {
            using Channel = devices::scc::Z8530Scc::Channel;
            switch (offset & 6U) {
            case 6: m_scc.writeData(Channel::A, value); break;
            case 4: m_scc.writeData(Channel::B, value); break;
            case 2: m_scc.writeControl(Channel::A, value); break;
            default: m_scc.writeControl(Channel::B, value); break;
            }
            break;
        }
        if (offset == 0x14018U) {
            auto current = static_cast<std::uint8_t>(m_amicRegisters[0x14018]);
            current &= static_cast<std::uint8_t>(~(value & 0xf0U));
            current = static_cast<std::uint8_t>((current & 0xf1U) | (value & 0x0eU));
            m_amicRegisters[0x14018] = static_cast<char>(current);
        } else {
            m_amicRegisters[static_cast<qsizetype>(offset)] = static_cast<char>(value);
            if (offset == 0x14010U && (value & 1U)) m_soundDmaStartCycle = m_scheduler.now();
        }
        break;
    }
    case BusRegion::NuBus: m_nubus.write8(address, value); break;
    case BusRegion::Unmapped: ++m_unmappedAccessCount; break;
    default: break;
    }
}

bool PowerMac8100Machine::handleProtectedWrite(std::uint32_t address, std::uint32_t, unsigned)
{
    // The PDM ROM's embedded 68k emulator presents the legacy ASC aperture at
    // 0x40800000.  Writes normally fault into its compatibility glue; AMIC's
    // startup-sound path only needs the FIFO/data strobes to complete.
    if (address >= 0x40800000U && address < 0x40801000U) return true;
    // The embedded 68k BlockMove used by the startup-sound path writes the
    // AMIC sound controls through their physical address after translation is
    // enabled, without installing a PTE for that page.  Real PDM hardware
    // completes the cycle; reflecting it as a 68k bus error drops into the ROM
    // monitor before video/SCSI bring-up.
    return address >= amicBase && address < amicBase + 0x40000U;
}

std::optional<std::uint32_t> PowerMac8100Machine::compatibilityRead(
    std::uint32_t address, unsigned size, std::uint32_t instructionPc)
{
    // While the embedded 68k emulator is active, legacy low-memory accesses
    // target motherboard RAM even though the 601 still has the boot-ROM BAT
    // covering effective address zero. This includes stack reads: allowing
    // those through the BAT makes RTS fetch return addresses from ROM.
    const auto ramSize = static_cast<std::uint32_t>(m_ram.size());
    const auto lowRamAccess = address < ramSize && size <= ramSize - address;
    if (instructionPc >= 0x68000000U && instructionPc < 0x68100000U
        && lowRamAccess) {
        std::uint32_t value = 0;
        for (unsigned byte = 0; byte < size; ++byte) {
            const auto source = m_compatibilityRamActive
                ? static_cast<std::uint8_t>(m_ram[static_cast<qsizetype>(address + byte)])
                : readMapped8(romBase + address + byte);
            value = (value << 8) | source;
        }
        return value;
    }
    // As with writes from the embedded 68k BlockMove, the startup-sound code
    // reaches AMIC through its physical aperture without a hashed-page entry.
    // These are real device reads, not fabricated ASC data.
    if (address >= amicBase && address < amicBase + 0x40000U) {
        std::uint32_t value = 0;
        for (unsigned byte = 0; byte < size; ++byte)
            value = (value << 8) | readMapped8(address + byte);
        return value;
    }
    // The PDM 68k compatibility map exposes onboard video at 0x60b00000.
    // HMC VBASE selects which of the first two physical megabytes backs that
    // legacy aperture; it is not an empty NuBus slot cycle.
    if (instructionPc >= 0x68000000U && instructionPc < 0x68100000U
        && address >= 0x60b00000U && address < 0x60c00000U) {
        const auto base = (m_hmcControl & 0x200000000ULL) ? 0U : 0x100000U;
        const auto offset = base + address - 0x60b00000U;
        if (offset + size > static_cast<std::uint32_t>(m_videoRam.size())) return std::nullopt;
        std::uint32_t value = 0;
        for (unsigned byte = 0; byte < size; ++byte)
            value = (value << 8) | m_videoRam[static_cast<qsizetype>(offset + byte)];
        return value;
    }
    // The Slot Manager runs inside the embedded 68k environment and probes
    // both populated and empty NuBus slot apertures.  Empty NuBus cycles read
    // as open bus; they are not failed PPC translations/68k bus errors.
    // Do not intercept the nanokernel's own 0x68000000 logical segment: its
    // code, stack, and emulator state translate to RAM/ROM through BATs and
    // PTEs even though the same numeric range is a physical NuBus aperture.
    const bool nativeCompatibilitySegment = address >= 0x68000000U && address < 0x69000000U;
    if (devices::nubus::NuBusBus::standardSlot(address) >= 0
        || (!nativeCompatibilitySegment && address >= 0x60000000U && address < 0xf0000000U)) {
        std::uint32_t value = 0;
        for (unsigned byte = 0; byte < size; ++byte)
            value = (value << 8) | readMapped8(address + byte);
        return value;
    }
    const bool legacyView = address >= 0x40800800U && address < 0x40800808U;
    const bool romAliasedView = address >= 0x40000800U && address < 0x40000808U;
    // The PDM ROM's startup-sound routine presents the old ASC register window
    // at an effective address which also contains embedded 68k ROM code.  The
    // generic 68k load helper alone cannot distinguish those uses, so scope the
    // compatibility response to the routine that owns A6 as the ASC base.
    const auto emulated68kPc = m_cpu.registers().gpr[24];
    if ((legacyView || romAliasedView)
        && emulated68kPc >= 0x408e1a20U && emulated68kPc <= 0x408e1b58U) {
        const auto registerOffset = address & 7U;
        ++m_ascCompatibilityReadCounts[registerOffset];
        m_ascCompatibilityReadCallers[registerOffset] = instructionPc;
        (void)size;
        // ID 0xe9 selects ASC-compatible output; service bits 0 and 1 mark both
        // output paths ready.  Values are left-justified by the CPU load path.
        return registerOffset < 4U ? 0xe9e9e9e9U : 0x03030303U;
    }
    if (address >= 0x40800000U && address < 0x40c00000U) {
        std::uint32_t value = 0;
        for (unsigned byte = 0; byte < size; ++byte)
            value = (value << 8) | readMapped8(romBase + ((address + byte - 0x40800000U) & (romSize - 1U)));
        return value;
    }
    return std::nullopt;
}

bool PowerMac8100Machine::compatibilityWrite(
    std::uint32_t address, std::uint32_t value, unsigned size, std::uint32_t instructionPc)
{
    // The PDM ROM keeps its read-only ROM BAT over logical address zero while
    // its embedded 68k emulator is active. Legacy stores in motherboard RAM
    // are compatibility cycles to physical RAM rather than writes to that BAT.
    const auto ramSize = static_cast<std::uint32_t>(m_ram.size());
    if (instructionPc < 0x68000000U || instructionPc >= 0x68100000U) return false;
    if (address >= 0x60b00000U && address < 0x60c00000U) {
        const auto base = (m_hmcControl & 0x200000000ULL) ? 0U : 0x100000U;
        const auto offset = base + address - 0x60b00000U;
        if (offset + size > static_cast<std::uint32_t>(m_videoRam.size())) return false;
        for (unsigned byte = 0; byte < size; ++byte) {
            const auto shift = (size - 1U - byte) * 8U;
            m_videoRam[static_cast<qsizetype>(offset + byte)] = static_cast<std::uint8_t>(value >> shift);
        }
        return true;
    }
    const bool nativeCompatibilitySegment = address >= 0x68000000U && address < 0x69000000U;
    if (devices::nubus::NuBusBus::standardSlot(address) >= 0
        || (!nativeCompatibilitySegment && address >= 0x60000000U && address < 0xf0000000U)) {
        for (unsigned byte = 0; byte < size; ++byte) {
            const auto shift = (size - 1U - byte) * 8U;
            m_nubus.write8(address + byte, static_cast<std::uint8_t>(value >> shift));
        }
        return true;
    }
    if (address >= ramSize || size > ramSize - address) return false;
    for (unsigned byte = 0; byte < size; ++byte) {
        const auto shift = (size - 1U - byte) * 8U;
        m_ram[static_cast<qsizetype>(address + byte)] = static_cast<char>(value >> shift);
    }
    m_compatibilityRamActive = true;
    return true;
}

void PowerMac8100Machine::recordBus(std::uint32_t address, std::uint32_t value, unsigned size,
                                    bool write, BusRegion region)
{
    if (!m_busTraceEnabled) return;
    if (m_busTrace.size() == maxBusTrace) m_busTrace.pop_front();
    m_busTrace.push_back({ m_scheduler.now(), m_cpu.programCounter(), address, value,
        static_cast<std::uint8_t>(size), write, region });
}

std::uint8_t PowerMac8100Machine::read8(std::uint32_t address)
{
    const auto region = regionFor(address);
    const auto value = readMapped8(address);
    recordBus(address, value, 1, false, region);
    return value;
}

std::uint16_t PowerMac8100Machine::read16(std::uint32_t address)
{
    const auto region = regionFor(address);
    std::uint16_t value;
    if (address == 0x50f10100U || address == 0x50f11100U) {
        flushDevices();
        value = address == 0x50f10100U ? m_scsi.readDmaWord() : m_scsiB.readDmaWord();
        updateInterrupts();
    } else if (region == BusRegion::Ram) {
        const auto index = *ramIndex(address);
        value = static_cast<std::uint16_t>((m_ram[static_cast<qsizetype>(index)] << 8)
            | m_ram[static_cast<qsizetype>(index + 1)]);
    } else if (region == BusRegion::Rom) {
        const auto index = (address - romBase) & (romSize - 1U);
        value = m_romLoaded ? static_cast<std::uint16_t>((static_cast<std::uint8_t>(m_rom.at(index)) << 8)
            | static_cast<std::uint8_t>(m_rom.at(index + 1))) : 0xffffU;
    } else {
        value = static_cast<std::uint16_t>((readMapped8(address) << 8) | readMapped8(address + 1));
    }
    recordBus(address, value, 2, false, region);
    return value;
}

std::uint32_t PowerMac8100Machine::read32(std::uint32_t address)
{
    const auto region = regionFor(address);
    std::uint32_t value;
    if (address == machineIdAddress) {
        value = machineIdValue & 0xffffU;
    } else if (region == BusRegion::Ram) {
        const auto index = *ramIndex(address);
        const auto* bytes = m_ram.constData() + index;
        value = (static_cast<std::uint32_t>(bytes[0]) << 24) | (static_cast<std::uint32_t>(bytes[1]) << 16)
            | (static_cast<std::uint32_t>(bytes[2]) << 8) | bytes[3];
    } else if (region == BusRegion::Rom && m_romLoaded) {
        const auto index = (address - romBase) & (romSize - 1U);
        const auto* bytes = reinterpret_cast<const std::uint8_t*>(m_rom.constData()) + index;
        value = (static_cast<std::uint32_t>(bytes[0]) << 24) | (static_cast<std::uint32_t>(bytes[1]) << 16)
            | (static_cast<std::uint32_t>(bytes[2]) << 8) | bytes[3];
    } else {
        value = (static_cast<std::uint32_t>(readMapped8(address)) << 24)
            | (static_cast<std::uint32_t>(readMapped8(address + 1)) << 16)
            | (static_cast<std::uint32_t>(readMapped8(address + 2)) << 8) | readMapped8(address + 3);
    }
    recordBus(address, value, 4, false, region);
    return value;
}

void PowerMac8100Machine::write8(std::uint32_t address, std::uint8_t value)
{
    const auto region = regionFor(address); writeMapped8(address, value); recordBus(address, value, 1, true, region);
}
void PowerMac8100Machine::write16(std::uint32_t address, std::uint16_t value)
{
    const auto region = regionFor(address);
    if (address == 0x50f10100U || address == 0x50f11100U) {
        flushDevices();
        if (address == 0x50f10100U) m_scsi.writeDmaWord(value); else m_scsiB.writeDmaWord(value);
        updateInterrupts();
    } else if (region == BusRegion::Ram) {
        const auto index = *ramIndex(address);
        m_ram[static_cast<qsizetype>(index)] = static_cast<std::uint8_t>(value >> 8);
        m_ram[static_cast<qsizetype>(index + 1)] = static_cast<std::uint8_t>(value);
    } else {
        writeMapped8(address, static_cast<std::uint8_t>(value >> 8));
        writeMapped8(address + 1, static_cast<std::uint8_t>(value));
    }
    recordBus(address, value, 2, true, region);
}
void PowerMac8100Machine::write32(std::uint32_t address, std::uint32_t value)
{
    const auto region = regionFor(address);
    if (region == BusRegion::Ram) {
        const auto index = *ramIndex(address);
        auto* bytes = m_ram.data() + index;
        bytes[0] = static_cast<std::uint8_t>(value >> 24); bytes[1] = static_cast<std::uint8_t>(value >> 16);
        bytes[2] = static_cast<std::uint8_t>(value >> 8); bytes[3] = static_cast<std::uint8_t>(value);
    } else {
        writeMapped8(address, static_cast<std::uint8_t>(value >> 24)); writeMapped8(address + 1, static_cast<std::uint8_t>(value >> 16));
        writeMapped8(address + 2, static_cast<std::uint8_t>(value >> 8)); writeMapped8(address + 3, static_cast<std::uint8_t>(value));
    }
    recordBus(address, value, 4, true, region);
}

QStringList PowerMac8100Machine::debugRegisterLines() const { return m_cpu.debugRegisterLines(); }
QString PowerMac8100Machine::disassemble(std::uint32_t address) const { return m_cpu.disassemble(address); }
std::uint8_t PowerMac8100Machine::debugRead8(std::uint32_t address) const { return const_cast<PowerMac8100Machine*>(this)->readMapped8(address); }
std::uint16_t PowerMac8100Machine::debugRead16(std::uint32_t address) const
{ return static_cast<std::uint16_t>((debugRead8(address) << 8) | debugRead8(address + 1)); }
std::uint32_t PowerMac8100Machine::debugRead32(std::uint32_t address) const
{ return address == machineIdAddress ? machineIdValue & 0xffffU
    : (static_cast<std::uint32_t>(debugRead16(address)) << 16) | debugRead16(address + 2); }
void PowerMac8100Machine::setBusTraceEnabled(bool enabled) { m_busTraceEnabled = enabled; if (!enabled) m_busTrace.clear(); }
void PowerMac8100Machine::setCpuTraceEnabled(bool enabled)
{ m_cpuTraceEnabled = enabled; m_cpuTraceFrozen = false; if (!enabled) m_cpuTrace.clear(); }

} // namespace cutemac::machines::powermac8100
