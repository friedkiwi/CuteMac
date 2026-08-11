#include "cutemac/debug/SnapshotMachine.h"

#include <utility>

namespace cutemac::debug {

namespace {

cpu::m68k::M68kCpuCore::Model modelFor(const QString& architecture)
{
    using Model = cpu::m68k::M68kCpuCore::Model;
    if (architecture.contains(QStringLiteral("68040"))) return Model::M68040;
    if (architecture.contains(QStringLiteral("68030"))) return Model::M68030;
    if (architecture.contains(QStringLiteral("68020"))) return Model::M68020;
    if (architecture.contains(QStringLiteral("68010"))) return Model::M68010;
    return Model::M68000;
}

bool isM68k(const QString& architecture)
{
    return architecture.startsWith(QStringLiteral("m68k"));
}

} // namespace

// Serves reads out of the captured regions. Disassembly goes through a real
// core bound to this bus, so a loaded dump disassembles anywhere it has bytes
// rather than only inside the window that happened to be rendered at capture.
class SnapshotMachine::SnapshotBus final : public cpu::m68k::M68kBus {
public:
    SnapshotBus(const QVector<MemoryRegion>& regions, std::uint64_t& unmappedReads)
        : m_regions(regions)
        , m_unmappedReads(unmappedReads)
    {
    }

    [[nodiscard]] std::uint8_t read8(std::uint32_t address) override { return readByte(address); }

    [[nodiscard]] std::uint16_t read16(std::uint32_t address) override
    {
        return static_cast<std::uint16_t>((readByte(address) << 8) | readByte(address + 1));
    }

    [[nodiscard]] std::uint32_t read32(std::uint32_t address) override
    {
        return (static_cast<std::uint32_t>(read16(address)) << 16) | read16(address + 2);
    }

    void write8(std::uint32_t, std::uint8_t) override {}
    void write16(std::uint32_t, std::uint16_t) override {}
    void write32(std::uint32_t, std::uint32_t) override {}

    [[nodiscard]] std::uint8_t readByte(std::uint32_t address) const
    {
        for (const auto& region : m_regions) {
            if (!region.readable || region.contents.isEmpty()) continue;
            if (address < region.base) continue;
            const auto offset = address - region.base;
            if (offset >= region.length) continue;
            if (offset >= static_cast<std::uint32_t>(region.contents.size())) continue;
            return static_cast<std::uint8_t>(region.contents.at(static_cast<qsizetype>(offset)));
        }
        ++m_unmappedReads;
        return 0xff;
    }

private:
    const QVector<MemoryRegion>& m_regions;
    std::uint64_t& m_unmappedReads;
};

SnapshotMachine::SnapshotMachine(MachineSnapshot snapshot)
    : m_snapshot(std::move(snapshot))
{
    m_bus = std::make_unique<SnapshotBus>(m_snapshot.memory, m_unmappedReads);
    if (isM68k(m_snapshot.cpu.architecture)) {
        m_m68k = std::make_unique<cpu::m68k::M68kCpuCore>();
        m_m68k->setModel(modelFor(m_snapshot.cpu.architecture));
        m_m68k->setBus(m_bus.get());
    }
}

SnapshotMachine::~SnapshotMachine() = default;

QString SnapshotMachine::debugCpuArchitecture() const
{
    return m_snapshot.cpu.architecture.isEmpty() ? QStringLiteral("unknown") : m_snapshot.cpu.architecture;
}

QStringList SnapshotMachine::debugRegisterLines() const { return m_snapshot.cpu.registerLines; }

int SnapshotMachine::runCycles(int) { return 0; }

int SnapshotMachine::stepInstruction() { return 0; }

std::uint32_t SnapshotMachine::programCounter() const { return m_snapshot.cpu.pc; }

QString SnapshotMachine::disassemble(std::uint32_t address) const
{
    if (m_m68k) {
        // Setting the core's PC is what binds Musashi's disassembler globals to
        // this bus; the core is never executed.
        return m_m68k->disassemble(address);
    }
    // Non-68k dumps fall back to the window rendered at capture time. Saying so
    // is better than inventing an instruction stream.
    const auto prefix = QStringLiteral("0x%1").arg(address, 8, 16, QLatin1Char('0'));
    for (const auto& line : m_snapshot.cpu.disassembly) {
        if (line.startsWith(prefix)) return line.mid(prefix.size()).trimmed();
    }
    return QStringLiteral("<outside the captured disassembly window>");
}

int SnapshotMachine::disassembleBytes(std::uint32_t address) const
{
    if (m_m68k) return m_m68k->disassembleBytes(address);
    return 4;
}

std::uint8_t SnapshotMachine::debugRead8(std::uint32_t address) const
{
    return m_bus->readByte(address);
}

std::uint16_t SnapshotMachine::debugRead16(std::uint32_t address) const
{
    return static_cast<std::uint16_t>((debugRead8(address) << 8) | debugRead8(address + 1));
}

std::uint32_t SnapshotMachine::debugRead32(std::uint32_t address) const
{
    return (static_cast<std::uint32_t>(debugRead16(address)) << 16) | debugRead16(address + 2);
}

void SnapshotMachine::debugWrite8(std::uint32_t, std::uint8_t) { m_writeAttempted = true; }
void SnapshotMachine::debugWrite16(std::uint32_t, std::uint16_t) { m_writeAttempted = true; }
void SnapshotMachine::debugWrite32(std::uint32_t, std::uint32_t) { m_writeAttempted = true; }

} // namespace cutemac::debug
