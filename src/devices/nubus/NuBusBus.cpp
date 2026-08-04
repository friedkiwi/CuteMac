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
    int slot = -1;
    if ((address & 0xf0000000U) == 0xf0000000U) slot = static_cast<int>((address >> 24) & 0x0f);
    else if (address >= 0x00900000U && address <= 0x00efffffU) slot = static_cast<int>((address >> 20) & 0x0f);
    return slot >= 9 && slot <= 14 ? slot : -1;
}

int NuBusBus::superSlot(std::uint32_t address)
{
    // NuBus Power Macs expose each slot through a 256 MiB super-slot window.
    // Cards which only decode the traditional 16 MiB slot space see that
    // space repeated throughout the window.
    if (address < 0x60000000U || address >= 0xf0000000U) return -1;
    const auto slot = static_cast<int>(address >> 28);
    return slot >= 9 && slot <= 14 ? slot : -1;
}

namespace {
int decodedSlot(std::uint32_t address)
{
    const auto standard = NuBusBus::standardSlot(address);
    return standard >= 0 ? standard : NuBusBus::superSlot(address);
}

std::uint32_t cardOffset(std::uint32_t address)
{
    return address < 0x01000000U ? address & 0x000fffffU : address & 0x00ffffffU;
}
}

std::uint8_t NuBusBus::read8(std::uint32_t address)
{
    const auto slot = decodedSlot(address);
    const auto target = card(slot);
    const auto offset = cardOffset(address);
    return target ? target->read8(offset) : 0xff;
}

std::uint16_t NuBusBus::read16(std::uint32_t address)
{
    const auto slot = decodedSlot(address);
    const auto target = card(slot);
    const auto offset = cardOffset(address);
    return target ? target->read16(offset) : 0xffffU;
}

std::uint32_t NuBusBus::read32(std::uint32_t address)
{
    const auto slot = decodedSlot(address);
    const auto target = card(slot);
    const auto offset = cardOffset(address);
    return target ? target->read32(offset) : 0xffffffffU;
}

void NuBusBus::write8(std::uint32_t address, std::uint8_t value)
{
    const auto slot = decodedSlot(address);
    const auto target = card(slot);
    const auto offset = cardOffset(address);
    if (target) target->write8(offset, value);
}

void NuBusBus::write16(std::uint32_t address, std::uint16_t value)
{
    const auto target = card(decodedSlot(address));
    const auto offset = cardOffset(address);
    if (target) target->write16(offset, value);
}

void NuBusBus::write32(std::uint32_t address, std::uint32_t value)
{
    const auto target = card(decodedSlot(address));
    const auto offset = cardOffset(address);
    if (target) target->write32(offset, value);
}


} // namespace cutemac::devices::nubus
