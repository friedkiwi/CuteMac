#include "cutemac/devices/nubus/NuBusBus.h"

namespace cutemac::devices::nubus {

bool NuBusBus::install(int slot, std::shared_ptr<NuBusCard> card)
{
    if (slot < 9 || slot > 14 || !card) return false;
    m_cards[static_cast<std::size_t>(slot)] = std::move(card);
    m_cards[static_cast<std::size_t>(slot)]->setIrqCallback([this, slot](bool asserted) {
        if (m_slotIrqCallback) m_slotIrqCallback(slot, asserted);
    });
    return true;
}

void NuBusBus::remove(int slot)
{
    if (slot < 0 || slot >= static_cast<int>(m_cards.size())) return;
    m_cards[static_cast<std::size_t>(slot)].reset();
}

std::shared_ptr<NuBusCard> NuBusBus::card(int slot) const
{
    if (slot < 0 || slot >= static_cast<int>(m_cards.size())) return {};
    return m_cards[static_cast<std::size_t>(slot)];
}

void NuBusBus::reset()
{
    for (const auto& card : m_cards) {
        if (card) card->reset();
    }
}

void NuBusBus::tick(std::uint64_t cycles)
{
    for (const auto& card : m_cards) {
        if (card) card->tick(cycles);
    }
}

int NuBusBus::standardSlot(std::uint32_t address)
{
    if ((address & 0xf0000000U) != 0xf0000000U) return -1;
    const auto slot = static_cast<int>((address >> 24) & 0x0f);
    return slot >= 9 && slot <= 14 ? slot : -1;
}

std::uint8_t NuBusBus::read8(std::uint32_t address)
{
    const auto slot = standardSlot(address);
    const auto target = card(slot);
    return target ? target->read8(address & 0x00ffffffU) : 0xff;
}

void NuBusBus::write8(std::uint32_t address, std::uint8_t value)
{
    const auto slot = standardSlot(address);
    const auto target = card(slot);
    if (target) target->write8(address & 0x00ffffffU, value);
}

} // namespace cutemac::devices::nubus
