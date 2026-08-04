#pragma once

#include <cstdint>

namespace cutemac::cpu::m68k {

class M68kBus {
public:
    template<typename T>
    struct ReadResult {
        T value {};
        bool busError = false;
    };

    virtual ~M68kBus() = default;

    [[nodiscard]] virtual std::uint8_t read8(std::uint32_t address) = 0;
    [[nodiscard]] virtual std::uint16_t read16(std::uint32_t address) = 0;
    [[nodiscard]] virtual std::uint32_t read32(std::uint32_t address) = 0;

    virtual void write8(std::uint32_t address, std::uint8_t value) = 0;
    virtual void write16(std::uint32_t address, std::uint16_t value) = 0;
    virtual void write32(std::uint32_t address, std::uint32_t value) = 0;

    // Result-bearing physical accesses let the CPU distinguish an actual bus
    // fault from intentionally returned open-bus data. Existing machines keep
    // their behavior through these defaults and can override faulting regions.
    [[nodiscard]] virtual ReadResult<std::uint8_t> readPhysical8(std::uint32_t address)
    {
        return { read8(address), false };
    }
    [[nodiscard]] virtual ReadResult<std::uint16_t> readPhysical16(std::uint32_t address)
    {
        return { read16(address), false };
    }
    [[nodiscard]] virtual ReadResult<std::uint32_t> readPhysical32(std::uint32_t address)
    {
        return { read32(address), false };
    }
    [[nodiscard]] virtual ReadResult<std::uint8_t> readProgram8(std::uint32_t address)
    {
        return readPhysical8(address);
    }
    [[nodiscard]] virtual ReadResult<std::uint16_t> readProgram16(std::uint32_t address)
    {
        return readPhysical16(address);
    }
    [[nodiscard]] virtual ReadResult<std::uint32_t> readProgram32(std::uint32_t address)
    {
        return readPhysical32(address);
    }
    virtual bool writePhysical8(std::uint32_t address, std::uint8_t value)
    {
        write8(address, value);
        return true;
    }
    virtual bool writePhysical16(std::uint32_t address, std::uint16_t value)
    {
        write16(address, value);
        return true;
    }
    virtual bool writePhysical32(std::uint32_t address, std::uint32_t value)
    {
        write32(address, value);
        return true;
    }
};

} // namespace cutemac::cpu::m68k
