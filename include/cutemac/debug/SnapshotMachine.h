#pragma once

#include <cstdint>
#include <memory>

#include <QString>
#include <QStringList>

#include "cutemac/core/IDebugCpuAccess.h"
#include "cutemac/cpu/m68k/M68kBus.h"
#include "cutemac/cpu/m68k/M68kCpuCore.h"
#include "cutemac/debug/MachineSnapshot.h"

namespace cutemac::debug {

// A frozen machine reconstructed from a panic archive. It satisfies the same
// debug boundary as a live machine, so the debug session's register, memory,
// disassembly, screen, and low-memory commands work against a dump unchanged.
// Execution and writes are refused rather than faked.
class SnapshotMachine final : public core::IDebugCpuAccess {
public:
    explicit SnapshotMachine(MachineSnapshot snapshot);
    ~SnapshotMachine() override;

    SnapshotMachine(const SnapshotMachine&) = delete;
    SnapshotMachine& operator=(const SnapshotMachine&) = delete;

    [[nodiscard]] QString debugCpuArchitecture() const override;
    [[nodiscard]] QStringList debugRegisterLines() const override;
    [[nodiscard]] int runCycles(int cycles) override;
    [[nodiscard]] int stepInstruction() override;
    [[nodiscard]] std::uint32_t programCounter() const override;
    [[nodiscard]] QString disassemble(std::uint32_t address) const override;
    [[nodiscard]] int disassembleBytes(std::uint32_t address) const override;
    [[nodiscard]] std::uint8_t debugRead8(std::uint32_t address) const override;
    [[nodiscard]] std::uint16_t debugRead16(std::uint32_t address) const override;
    [[nodiscard]] std::uint32_t debugRead32(std::uint32_t address) const override;
    void debugWrite8(std::uint32_t address, std::uint8_t value) override;
    void debugWrite16(std::uint32_t address, std::uint16_t value) override;
    void debugWrite32(std::uint32_t address, std::uint32_t value) override;

    [[nodiscard]] const MachineSnapshot& snapshot() const { return m_snapshot; }
    // Addresses outside every captured region read as open bus; the counter
    // tells a reader how much of what they are looking at was never captured.
    [[nodiscard]] std::uint64_t unmappedReads() const { return m_unmappedReads; }
    [[nodiscard]] bool writeAttempted() const { return m_writeAttempted; }

private:
    class SnapshotBus;

    MachineSnapshot m_snapshot;
    std::unique_ptr<SnapshotBus> m_bus;
    std::unique_ptr<cpu::m68k::M68kCpuCore> m_m68k;
    mutable std::uint64_t m_unmappedReads = 0;
    mutable bool m_writeAttempted = false;
};

} // namespace cutemac::debug
