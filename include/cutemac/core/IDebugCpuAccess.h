#pragma once

#include <cstdint>

#include <QString>
#include <QStringList>

namespace cutemac::core {

class IDebugCpuAccess {
public:
    virtual ~IDebugCpuAccess() = default;

    [[nodiscard]] virtual QString debugCpuArchitecture() const = 0;
    [[nodiscard]] virtual QStringList debugRegisterLines() const = 0;
    [[nodiscard]] virtual int runCycles(int cycles) = 0;
    [[nodiscard]] virtual int stepInstruction() = 0;
    [[nodiscard]] virtual std::uint32_t programCounter() const = 0;
    [[nodiscard]] virtual QString disassemble(std::uint32_t address) const = 0;
    [[nodiscard]] virtual int disassembleBytes(std::uint32_t address) const = 0;
    [[nodiscard]] virtual std::uint8_t debugRead8(std::uint32_t address) const = 0;
    [[nodiscard]] virtual std::uint16_t debugRead16(std::uint32_t address) const = 0;
    [[nodiscard]] virtual std::uint32_t debugRead32(std::uint32_t address) const = 0;
    virtual void debugWrite8(std::uint32_t address, std::uint8_t value) = 0;
    virtual void debugWrite16(std::uint32_t address, std::uint16_t value) = 0;
    virtual void debugWrite32(std::uint32_t address, std::uint32_t value) = 0;

    // Logical to physical through the guest's own translation tables,
    // without side effects. Machines with no MMU answer with the address
    // unchanged, which is what their hardware does.
    [[nodiscard]] virtual std::uint32_t debugTranslate(std::uint32_t logical) const { return logical; }
};

} // namespace cutemac::core
