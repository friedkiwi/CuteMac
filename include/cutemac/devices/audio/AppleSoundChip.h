#pragma once

#include <array>
#include <cstdint>

namespace cutemac::devices::audio {

class AppleSoundChip {
public:
    void reset();
    [[nodiscard]] std::uint8_t read(std::uint16_t offset);
    void write(std::uint16_t offset, std::uint8_t value);
    void tick(std::uint64_t cpuCycles);
    [[nodiscard]] bool interruptActive() const;

private:
    std::array<std::uint8_t, 0x1000> m_memory {};
    std::array<std::array<std::uint8_t, 0x400>, 2> m_fifo {};
    std::array<std::uint16_t, 2> m_readPointer {};
    std::array<std::uint16_t, 2> m_writePointer {};
    std::array<std::uint16_t, 2> m_capacity {};
    std::uint64_t m_sampleCycleAccumulator = 0;
    bool m_irq = false;
};

} // namespace cutemac::devices::audio
