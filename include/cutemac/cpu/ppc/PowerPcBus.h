#pragma once

#include <cstdint>
#include <optional>

namespace cutemac::cpu::ppc {

class PowerPcBus {
public:
    virtual ~PowerPcBus() = default;

    [[nodiscard]] virtual std::uint8_t read8(std::uint32_t physicalAddress) = 0;
    [[nodiscard]] virtual std::uint16_t read16(std::uint32_t physicalAddress) = 0;
    [[nodiscard]] virtual std::uint32_t read32(std::uint32_t physicalAddress) = 0;

    virtual void write8(std::uint32_t physicalAddress, std::uint8_t value) = 0;
    virtual void write16(std::uint32_t physicalAddress, std::uint16_t value) = 0;
    virtual void write32(std::uint32_t physicalAddress, std::uint32_t value) = 0;

    // Models TEA from the physical bus. The 601 turns this into a machine
    // check, distinct from an MMU translation or protection fault.
    [[nodiscard]] virtual bool accessFault(
        std::uint32_t, unsigned, bool, bool) const { return false; }

    // Some systems use protection faults as a compatibility-I/O gateway.
    // Return true only when the bus has completed such a denied store.
    virtual bool handleProtectedWrite(std::uint32_t, std::uint32_t, unsigned) { return false; }
    virtual std::optional<std::uint32_t> compatibilityRead(
        std::uint32_t, unsigned, std::uint32_t) { return std::nullopt; }
    virtual bool compatibilityWrite(
        std::uint32_t, std::uint32_t, unsigned, std::uint32_t) { return false; }
};

} // namespace cutemac::cpu::ppc
