#include "cutemac/machines/powermac8100/PowerMac8100Machine.h"

#include <algorithm>

#include <QFile>

#include "cutemac/rom/RomPatcher.h"

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
constexpr std::size_t maxBusTrace = 4096;
}

PowerMac8100Machine::PowerMac8100Machine(std::size_t ramSize)
    : m_ram(static_cast<qsizetype>(std::max<std::size_t>(ramSize, 8U * 1024U * 1024U)), 0)
    , m_amicRegisters(0x40000, 0)
{
    m_cpu.setBus(this);
    m_cpu.setClockFrequency(80'000'000U);
}

QString PowerMac8100Machine::machineId() const { return QStringLiteral("powermac-8100"); }

void PowerMac8100Machine::attachSerialEndpoint(int channel, std::shared_ptr<devices::serial::SerialEndpoint> endpoint)
{
    m_scc.attachEndpoint(channel == 0 ? devices::scc::Z8530Scc::Channel::A : devices::scc::Z8530Scc::Channel::B, std::move(endpoint));
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
    std::fill(m_ram.begin(), m_ram.end(), 0);
    m_scheduler.reset();
    m_scc.reset();
    m_hmcControl = 0;
    m_hmcShiftBuffer = 0;
    m_hmcBitPosition = 0;
    m_hmcCommitted = false;
    m_soundDmaStartCycle = 0;
    m_amicRegisters.fill(0);
    m_unmappedAccessCount = 0;
    m_busTrace.clear();
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
    m_scheduler.dispatchDue();
    const auto cycles = std::max(1, m_cpu.stepInstruction());
    m_cpu.advanceTime(static_cast<std::uint32_t>(cycles));
    m_scc.tick(cycles);
    m_scheduler.advance(static_cast<std::uint64_t>(cycles));
    return cycles;
}

std::uint64_t PowerMac8100Machine::cycleCount() const { return m_scheduler.now(); }
std::uint32_t PowerMac8100Machine::programCounter() const { return m_cpu.programCounter(); }

PowerMac8100Machine::BusRegion PowerMac8100Machine::regionFor(std::uint32_t address) const
{
    if (ramIndex(address)) return BusRegion::Ram;
    if ((address >= romBase && address < 0x50000000U)
        || address >= resetRomBase) return BusRegion::Rom;
    if (address >= hmcBase && address < hmcBase + 0x10000U) return BusRegion::Hmc;
    if (address >= amicBase && address < amicBase + 0x40000U) return BusRegion::Amic;
    if (address >= machineIdAddress) return BusRegion::MachineId;
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
        const auto offset = address - amicBase;
        if (offset >= 0x4000U && offset < 0x400cU) {
            using Channel = devices::scc::Z8530Scc::Channel;
            switch (offset & 6U) {
            case 6: return m_scc.readData(Channel::A);
            case 4: return m_scc.readData(Channel::B);
            case 2: return m_scc.readControl(Channel::A);
            default: return m_scc.readControl(Channel::B);
            }
        }
        if (offset >= 0x2c000U && offset < 0x2e000U) return offset == 0x2c000U ? 1U : 0U;
        if (offset >= 0x1400cU && offset <= 0x1400eU) return 0; // idle sound phase
        if (offset == 0x14018U && (static_cast<std::uint8_t>(m_amicRegisters[0x14010]) & 1U)
            && m_scheduler.now() - m_soundDmaStartCycle >= 4096U) {
            m_amicRegisters[0x14018] = static_cast<char>(static_cast<std::uint8_t>(m_amicRegisters[0x14018]) | 0xc0U);
        }
        return static_cast<std::uint8_t>(m_amicRegisters[static_cast<qsizetype>(offset)]);
    }
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
        const auto offset = address - amicBase;
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
    case BusRegion::Unmapped: ++m_unmappedAccessCount; break;
    default: break;
    }
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
    const auto value = static_cast<std::uint16_t>((readMapped8(address) << 8) | readMapped8(address + 1));
    recordBus(address, value, 2, false, region);
    return value;
}

std::uint32_t PowerMac8100Machine::read32(std::uint32_t address)
{
    const auto region = regionFor(address);
    const auto value = address == machineIdAddress ? machineIdValue & 0xffffU
        : (static_cast<std::uint32_t>(readMapped8(address)) << 24)
        | (static_cast<std::uint32_t>(readMapped8(address + 1)) << 16)
        | (static_cast<std::uint32_t>(readMapped8(address + 2)) << 8) | readMapped8(address + 3);
    recordBus(address, value, 4, false, region);
    return value;
}

void PowerMac8100Machine::write8(std::uint32_t address, std::uint8_t value)
{
    const auto region = regionFor(address); writeMapped8(address, value); recordBus(address, value, 1, true, region);
}
void PowerMac8100Machine::write16(std::uint32_t address, std::uint16_t value)
{
    const auto region = regionFor(address); writeMapped8(address, static_cast<std::uint8_t>(value >> 8));
    writeMapped8(address + 1, static_cast<std::uint8_t>(value)); recordBus(address, value, 2, true, region);
}
void PowerMac8100Machine::write32(std::uint32_t address, std::uint32_t value)
{
    const auto region = regionFor(address);
    writeMapped8(address, static_cast<std::uint8_t>(value >> 24)); writeMapped8(address + 1, static_cast<std::uint8_t>(value >> 16));
    writeMapped8(address + 2, static_cast<std::uint8_t>(value >> 8)); writeMapped8(address + 3, static_cast<std::uint8_t>(value));
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

} // namespace cutemac::machines::powermac8100
