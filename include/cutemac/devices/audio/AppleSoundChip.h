#pragma once

#include <array>
#include <cstdint>

namespace cutemac::devices::audio {

class AppleSoundChip {
public:
    void reset();
    [[nodiscard]] std::uint8_t read(std::uint16_t offset) const;
    void write(std::uint16_t offset, std::uint8_t value);
    [[nodiscard]] bool interruptActive() const;

private:
    std::array<std::uint8_t, 0x1000> m_memory {};
};

} // namespace cutemac::devices::audio
