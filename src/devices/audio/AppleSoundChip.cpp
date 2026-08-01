#include "cutemac/devices/audio/AppleSoundChip.h"

namespace cutemac::devices::audio {

void AppleSoundChip::reset()
{
    m_memory.fill(0);
}

std::uint8_t AppleSoundChip::read(std::uint16_t offset) const
{
    offset &= 0x0fff;
    if (offset == 0x800) return 0;
    return m_memory[offset];
}

void AppleSoundChip::write(std::uint16_t offset, std::uint8_t value)
{
    offset &= 0x0fff;
    if (offset != 0x800) m_memory[offset] = value;
}

bool AppleSoundChip::interruptActive() const
{
    return (m_memory[0x804] & 0x0f) != 0 && (m_memory[0x802] & 1) != 0;
}

} // namespace cutemac::devices::audio
