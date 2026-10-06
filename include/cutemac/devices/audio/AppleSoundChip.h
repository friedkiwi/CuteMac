#pragma once

#include <array>
#include <cstdint>

namespace cutemac::devices::audio {

class AppleSoundChip {
public:
    enum class Model {
        Original,
        Enhanced,
    };

    explicit AppleSoundChip(Model model = Model::Original)
        : m_model(model)
    {
    }

    void reset();
    [[nodiscard]] std::uint8_t read(std::uint16_t offset);
    void write(std::uint16_t offset, std::uint8_t value);
    void tick(std::uint64_t cpuCycles);
    [[nodiscard]] bool interruptActive() const;

private:
    Model m_model;
    std::array<std::uint8_t, 0x1000> m_memory {};
    std::array<std::array<std::uint8_t, 0x400>, 2> m_fifo {};
    std::array<std::uint16_t, 2> m_readPointer {};
    std::array<std::uint16_t, 2> m_writePointer {};
    std::array<std::uint16_t, 2> m_capacity {};
    std::uint64_t m_sampleCycleAccumulator = 0;
    bool m_irq = false;
};

} // namespace cutemac::devices::audio
