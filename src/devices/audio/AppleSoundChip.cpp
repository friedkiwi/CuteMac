#include "cutemac/devices/audio/AppleSoundChip.h"

namespace cutemac::devices::audio {

namespace {

constexpr std::uint16_t modeRegister = 0x801;
constexpr std::uint16_t controlRegister = 0x802;
constexpr std::uint16_t fifoModeRegister = 0x803;
constexpr std::uint16_t fifoStatusRegister = 0x804;
constexpr std::uint64_t cpuClock = 15'667'200;
constexpr std::uint64_t sampleRate = 22'257;

} // namespace

void AppleSoundChip::reset()
{
    m_memory.fill(0);
    for (auto& fifo : m_fifo) fifo.fill(0);
    m_readPointer.fill(0);
    m_writePointer.fill(0);
    m_capacity.fill(0);
    m_sampleCycleAccumulator = 0;
    m_irq = false;
}

std::uint8_t AppleSoundChip::read(std::uint16_t offset)
{
    offset &= 0x0fff;
    if (offset < 0x400) return m_fifo[0][offset];
    if (offset < 0x800) return m_fifo[1][offset - 0x400];
    if (offset == 0x800) return 0; // original ASC version
    if (offset == fifoStatusRegister) {
        const auto status = m_memory[offset];
        // The original ASC clears FIFO status and its IRQ when status is read.
        m_memory[offset] = 0;
        m_irq = false;
        return status;
    }
    return m_memory[offset];
}

void AppleSoundChip::write(std::uint16_t offset, std::uint8_t value)
{
    offset &= 0x0fff;
    if (offset < 0x800) {
        const auto channel = offset < 0x400 ? 0U : 1U;
        m_fifo[channel][m_writePointer[channel]] = value;
        m_writePointer[channel] = static_cast<std::uint16_t>((m_writePointer[channel] + 1) & 0x3ff);
        if (m_capacity[channel] < 0x400) ++m_capacity[channel];
        const auto halfBit = static_cast<std::uint8_t>(channel == 0 ? 0x01 : 0x04);
        const auto edgeBit = static_cast<std::uint8_t>(channel == 0 ? 0x02 : 0x08);
        if (m_capacity[channel] >= 0x200) m_memory[fifoStatusRegister] &= static_cast<std::uint8_t>(~halfBit);
        if (m_capacity[channel] >= 0x3ff) m_memory[fifoStatusRegister] |= edgeBit;
        else if (m_capacity[channel] > 0) m_memory[fifoStatusRegister] &= static_cast<std::uint8_t>(~edgeBit);
        return;
    }
    if (offset == 0x800) return;
    if (offset == modeRegister && (value & 3) != (m_memory[offset] & 3)) {
        m_readPointer.fill(0);
        m_writePointer.fill(0);
        m_capacity.fill(0);
        m_sampleCycleAccumulator = 0;
    }
    if (offset == fifoModeRegister && (value & 0x80) != 0) {
        m_readPointer.fill(0);
        m_writePointer.fill(0);
        m_capacity.fill(0);
        m_memory[fifoStatusRegister] |= 0x0a;
    }
    m_memory[offset] = value;
}

void AppleSoundChip::tick(std::uint64_t cpuCycles)
{
    if ((m_memory[modeRegister] & 3) != 1) return;
    m_sampleCycleAccumulator += cpuCycles * sampleRate;
    const auto samples = m_sampleCycleAccumulator / cpuClock;
    m_sampleCycleAccumulator %= cpuClock;
    const auto stereo = (m_memory[controlRegister] & 1) != 0;
    for (std::uint64_t sample = 0; sample < samples; ++sample) {
        const auto consume = [this](std::size_t channel) {
            const auto oldCapacity = m_capacity[channel];
            if (oldCapacity != 0) {
                m_readPointer[channel] = static_cast<std::uint16_t>((m_readPointer[channel] + 1) & 0x3ff);
                --m_capacity[channel];
            }
            if (oldCapacity == 0x1ff) {
                const auto halfBit = static_cast<std::uint8_t>(channel == 0 ? 0x01 : 0x04);
                const auto edgeBit = static_cast<std::uint8_t>(channel == 0 ? 0x02 : 0x08);
                m_memory[fifoStatusRegister] |= halfBit;
                m_memory[fifoStatusRegister] &= static_cast<std::uint8_t>(~edgeBit);
                m_irq = true;
            }
        };
        consume(0);
        if (stereo) consume(1);
    }
}

bool AppleSoundChip::interruptActive() const
{
    return m_irq;
}

} // namespace cutemac::devices::audio
