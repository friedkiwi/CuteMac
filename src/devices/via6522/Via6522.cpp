#include "cutemac/devices/via6522/Via6522.h"

namespace cutemac::devices::via6522 {

namespace {

constexpr std::uint8_t registerB = 0;
constexpr std::uint8_t dataDirectionB = 2;
constexpr std::uint8_t dataDirectionA = 3;
constexpr std::uint8_t timer1CounterLow = 4;
constexpr std::uint8_t timer1CounterHigh = 5;
constexpr std::uint8_t timer1LatchLow = 6;
constexpr std::uint8_t timer1LatchHigh = 7;
constexpr std::uint8_t timer2CounterLow = 8;
constexpr std::uint8_t timer2CounterHigh = 9;
constexpr std::uint8_t auxiliaryControl = 11;
constexpr std::uint8_t interruptFlag = 13;
constexpr std::uint8_t interruptEnable = 14;
constexpr std::uint8_t registerA = 15;

constexpr std::uint8_t initialPortA = 0x7b;
constexpr std::uint8_t initialPortB = 0x8f;
constexpr std::uint8_t initialDdrA = 0x7f;
constexpr std::uint8_t initialDdrB = 0x87;
constexpr std::uint8_t overlayBit = 0x10;
constexpr std::uint8_t timer1InterruptBit = 0x02;
constexpr std::uint8_t timer2InterruptBit = 0x20;

} // namespace

void Via6522::reset()
{
    m_registers.fill(0);
    m_registers[registerA] = initialPortA;
    m_registers[registerB] = initialPortB;
    m_registers[dataDirectionA] = initialDdrA;
    m_registers[dataDirectionB] = initialDdrB;
    m_interruptEnable = 0;
    m_timer1Counter = 0;
    m_timer1Latch = 0;
    m_timer1Running = false;
    m_timer2Counter = 0;
    m_timer2Running = false;
    notifyPortAChanged();
}

std::uint8_t Via6522::readRegister(std::uint8_t index)
{
    index &= 0x0f;
    if (index == interruptFlag) {
        return interruptFlagRegister();
    }
    if (index == interruptEnable) {
        return static_cast<std::uint8_t>(0x80 | m_interruptEnable);
    }

    return m_registers[index];
}

void Via6522::writeRegister(std::uint8_t index, std::uint8_t value)
{
    index &= 0x0f;
    if (index == interruptFlag) {
        m_registers[interruptFlag] &= static_cast<std::uint8_t>(~value);
        return;
    }
    if (index == interruptEnable) {
        if ((value & 0x80) != 0) {
            m_interruptEnable |= static_cast<std::uint8_t>(value & 0x7f);
        } else {
            m_interruptEnable &= static_cast<std::uint8_t>(~value);
        }
        return;
    }

    if (index == timer1CounterLow) {
        m_registers[index] = value;
        m_timer1Latch = (m_timer1Latch & 0xff00) | value;
        return;
    }
    if (index == timer1CounterHigh) {
        m_registers[index] = value;
        m_timer1Latch = (static_cast<int>(value) << 8) | (m_timer1Latch & 0x00ff);
        m_timer1Counter = m_timer1Latch == 0 ? 0x10000 : m_timer1Latch;
        m_timer1Running = true;
        m_registers[interruptFlag] &= static_cast<std::uint8_t>(~timer1InterruptBit);
        return;
    }
    if (index == timer1LatchLow) {
        m_registers[index] = value;
        m_timer1Latch = (m_timer1Latch & 0xff00) | value;
        return;
    }
    if (index == timer1LatchHigh) {
        m_registers[index] = value;
        m_timer1Latch = (static_cast<int>(value) << 8) | (m_timer1Latch & 0x00ff);
        return;
    }
    if (index == timer2CounterLow) {
        m_registers[index] = value;
        m_registers[interruptFlag] &= static_cast<std::uint8_t>(~timer2InterruptBit);
        return;
    }
    if (index == timer2CounterHigh) {
        m_registers[index] = value;
        m_timer2Counter = (static_cast<int>(m_registers[timer2CounterHigh]) << 8) | m_registers[timer2CounterLow];
        m_timer2Running = true;
        m_registers[interruptFlag] &= static_cast<std::uint8_t>(~timer2InterruptBit);
        return;
    }

    m_registers[index] = value;
    if (index == registerA) {
        notifyPortAChanged();
    }
}

void Via6522::tick(int cycles)
{
    if (cycles <= 0) {
        return;
    }

    if (m_timer1Running) {
        m_timer1Counter -= cycles;
        if (m_timer1Counter <= 0) {
            m_registers[interruptFlag] |= timer1InterruptBit;
            if ((m_registers[auxiliaryControl] & 0x40) != 0) {
                const auto reload = m_timer1Latch == 0 ? 0x10000 : m_timer1Latch;
                while (m_timer1Counter <= 0) {
                    m_timer1Counter += reload;
                }
            } else {
                m_timer1Counter = 0;
                m_timer1Running = false;
            }
        }
        const auto counter = static_cast<std::uint16_t>(std::max(0, m_timer1Counter));
        m_registers[timer1CounterLow] = static_cast<std::uint8_t>(counter);
        m_registers[timer1CounterHigh] = static_cast<std::uint8_t>(counter >> 8);
    }

    if (m_timer2Running) {
        m_timer2Counter -= cycles;
        if (m_timer2Counter > 0) {
            const auto counter = static_cast<std::uint16_t>(m_timer2Counter);
            m_registers[timer2CounterLow] = static_cast<std::uint8_t>(counter);
            m_registers[timer2CounterHigh] = static_cast<std::uint8_t>(counter >> 8);
            return;
        }

        m_timer2Counter = 0;
        m_timer2Running = false;
        m_registers[timer2CounterLow] = 0;
        m_registers[timer2CounterHigh] = 0;
        m_registers[interruptFlag] |= timer2InterruptBit;
    }
}

void Via6522::setPortAChangedCallback(PortAChangedCallback callback)
{
    m_portAChanged = std::move(callback);
}

std::uint8_t Via6522::portA() const
{
    return m_registers[registerA];
}

std::uint8_t Via6522::portB() const
{
    return m_registers[registerB];
}

void Via6522::setPortBInputBit(std::uint8_t bit, bool high)
{
    if (bit >= 8) {
        return;
    }

    const auto mask = static_cast<std::uint8_t>(1U << bit);
    if (high) {
        m_registers[registerB] |= mask;
    } else {
        m_registers[registerB] &= static_cast<std::uint8_t>(~mask);
    }
}

bool Via6522::overlayEnabled() const
{
    return (portA() & overlayBit) != 0;
}

bool Via6522::interruptActive() const
{
    return (m_registers[interruptFlag] & m_interruptEnable) != 0;
}

Via6522::DebugState Via6522::debugState() const
{
    return {
        m_registers[interruptFlag],
        m_interruptEnable,
        m_timer1Counter,
        m_timer1Running,
        m_timer2Counter,
        m_timer2Running,
        interruptActive(),
    };
}

void Via6522::notifyPortAChanged()
{
    if (m_portAChanged) {
        m_portAChanged(portA());
    }
}

std::uint8_t Via6522::interruptFlagRegister() const
{
    const auto active = static_cast<std::uint8_t>(m_registers[interruptFlag] & m_interruptEnable);
    return active == 0 ? m_registers[interruptFlag] : static_cast<std::uint8_t>(m_registers[interruptFlag] | 0x80);
}

} // namespace cutemac::devices::via6522
