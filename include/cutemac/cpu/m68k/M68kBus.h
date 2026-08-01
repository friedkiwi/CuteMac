#pragma once

#include <cstdint>

namespace cutemac::cpu::m68k {

class M68kBus {
public:
    virtual ~M68kBus() = default;

    [[nodiscard]] virtual std::uint8_t read8(std::uint32_t address) = 0;
    [[nodiscard]] virtual std::uint16_t read16(std::uint32_t address) = 0;
    [[nodiscard]] virtual std::uint32_t read32(std::uint32_t address) = 0;

    virtual void write8(std::uint32_t address, std::uint8_t value) = 0;
    virtual void write16(std::uint32_t address, std::uint16_t value) = 0;
    virtual void write32(std::uint32_t address, std::uint32_t value) = 0;
};

} // namespace cutemac::cpu::m68k
