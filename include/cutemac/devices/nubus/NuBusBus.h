#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <memory>

#include "cutemac/devices/nubus/NuBusCard.h"

namespace cutemac::devices::nubus {

class NuBusBus {
public:
    using SlotIrqCallback = std::function<void(int, bool)>;

    [[nodiscard]] bool install(int slot, std::shared_ptr<NuBusCard> card);
    void remove(int slot);
    [[nodiscard]] std::shared_ptr<NuBusCard> card(int slot) const;
    void reset();
    void tick(std::uint64_t cycles);
    [[nodiscard]] std::uint8_t read8(std::uint32_t address);
    void write8(std::uint32_t address, std::uint8_t value);
    void setSlotIrqCallback(SlotIrqCallback callback) { m_slotIrqCallback = std::move(callback); }

    [[nodiscard]] static int standardSlot(std::uint32_t address);

private:
    std::array<std::shared_ptr<NuBusCard>, 16> m_cards;
    SlotIrqCallback m_slotIrqCallback;
};

} // namespace cutemac::devices::nubus
