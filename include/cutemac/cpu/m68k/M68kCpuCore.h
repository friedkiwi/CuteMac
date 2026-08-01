#pragma once

#include <cstdint>

#include <array>
#include "cutemac/core/CpuCore.h"
#include "cutemac/cpu/m68k/M68kBus.h"

namespace cutemac::cpu::m68k {

class M68kCpuCore final : public core::CpuCore {
public:
    struct RegisterSnapshot {
        std::array<std::uint32_t, 8> d {};
        std::array<std::uint32_t, 8> a {};
        std::uint32_t pc = 0;
        std::uint16_t sr = 0;
        std::uint32_t usp = 0;
        std::uint32_t isp = 0;
        std::uint32_t msp = 0;
        std::uint32_t vbr = 0;
    };

    enum class Model {
        M68000,
        M68010,
        M68Ec020,
        M68020,
        M68Ec030,
        M68030,
        M68Ec040,
        M68Lc040,
        M68040,
    };

    M68kCpuCore();
    ~M68kCpuCore() override;

    M68kCpuCore(const M68kCpuCore&) = delete;
    M68kCpuCore& operator=(const M68kCpuCore&) = delete;

    [[nodiscard]] QString id() const override;
    void reset() override;

    void setModel(Model model);
    void setBus(M68kBus* bus);
    void setIrqLevel(unsigned int level);

    [[nodiscard]] int execute(int cycles);
    [[nodiscard]] int stepInstruction();
    [[nodiscard]] std::uint32_t programCounter() const;
    [[nodiscard]] RegisterSnapshot registers() const;
    [[nodiscard]] QString disassemble(std::uint32_t address) const;
    [[nodiscard]] int disassembleBytes(std::uint32_t address) const;

    void setProgramCounter(std::uint32_t address);

private:
    Model m_model = Model::M68000;
    M68kBus* m_bus = nullptr;
};

} // namespace cutemac::cpu::m68k
