#include "cutemac/devices/via6522/Via6522.h"

namespace cutemac::devices::via6522 {

namespace {

constexpr std::uint8_t registerB = 0;
constexpr std::uint8_t registerAHandshake = 1;
constexpr std::uint8_t dataDirectionB = 2;
constexpr std::uint8_t dataDirectionA = 3;
constexpr std::uint8_t timer1CounterLow = 4;
constexpr std::uint8_t timer1CounterHigh = 5;
constexpr std::uint8_t timer1LatchLow = 6;
constexpr std::uint8_t timer1LatchHigh = 7;
constexpr std::uint8_t timer2CounterLow = 8;
constexpr std::uint8_t timer2CounterHigh = 9;
constexpr std::uint8_t auxiliaryControl = 11;
constexpr std::uint8_t shiftRegister = 10;
constexpr std::uint8_t interruptFlag = 13;
constexpr std::uint8_t interruptEnable = 14;
constexpr std::uint8_t registerA = 15;

constexpr std::uint8_t overlayBit = 0x10;
constexpr std::uint8_t vblInterruptBit = 0x02;
constexpr std::uint8_t cb1InterruptBit = 0x10;
constexpr std::uint8_t cb2InterruptBit = 0x08;
constexpr std::uint8_t timer1InterruptBit = 0x40;
constexpr std::uint8_t timer2InterruptBit = 0x20;
constexpr std::uint8_t shiftRegisterInterruptBit = 0x04;
constexpr int keyboardByteCycles = 80;
constexpr int keyboardInquiryTimeoutCycles = 1958400;

} // namespace

void Via6522::reset()
{
    m_registers.fill(0);
    m_registers[registerA] = m_initialPortA;
    m_registers[registerB] = m_initialPortB;
    m_registers[dataDirectionA] = m_initialDdrA;
    m_registers[dataDirectionB] = m_initialDdrB;
    m_interruptEnable = 0;
    m_timer1Counter = 0;
    m_timer1Latch = 0;
    m_timer1Running = false;
    m_timer2Counter = 0;
    m_timer2Running = false;
    m_vblCycles = m_automaticCa1Period;
    m_ca1 = true;
    m_cb1 = true;
    m_cb2 = true;
    m_keyboardTransitions.clear();
    m_keyboardCommand = 0;
    m_shiftCycles = 0;
    m_keyboardCommandPending = false;
    m_keyboardResponseReady = false;
    notifyPortAChanged();
    if (m_portBChanged) m_portBChanged(m_registers[registerB], m_registers[dataDirectionB]);
}

std::uint8_t Via6522::readRegister(std::uint8_t index)
{
    index &= 0x0f;
    if (index == registerB) {
        m_registers[interruptFlag] &= static_cast<std::uint8_t>(~(cb1InterruptBit | cb2InterruptBit));
        return portB();
    }
    if (index == registerAHandshake) {
        m_registers[interruptFlag] &= static_cast<std::uint8_t>(~0x03U);
        return portA();
    }
    if (index == registerA) {
        return portA();
    }
    if (index == shiftRegister) {
        m_registers[interruptFlag] &= static_cast<std::uint8_t>(~shiftRegisterInterruptBit);
        const auto value = m_registers[shiftRegister];
        if (m_keyboardResponseReady) {
            m_keyboardResponseReady = false;
            m_keyboardCommandPending = false;
        } else if (m_keyboardCommandPending && (m_registers[auxiliaryControl] & 0x10) == 0 && m_shiftCycles == 0) {
            m_shiftCycles = m_keyboardCommand == 0x10 && m_keyboardTransitions.empty()
                ? keyboardInquiryTimeoutCycles
                : keyboardByteCycles;
        }
        return value;
    }
    if (index == timer1CounterLow) {
        m_registers[interruptFlag] &= static_cast<std::uint8_t>(~timer1InterruptBit);
        return m_registers[index];
    }
    if (index == timer2CounterLow) {
        m_registers[interruptFlag] &= static_cast<std::uint8_t>(~timer2InterruptBit);
        return m_registers[index];
    }
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
    if (index == shiftRegister) {
        m_registers[index] = value;
        m_registers[interruptFlag] &= static_cast<std::uint8_t>(~shiftRegisterInterruptBit);
        if ((m_registers[auxiliaryControl] & 0x1c) == 0x1c && !m_shiftRegisterWrite) {
            m_keyboardCommand = value;
            m_keyboardCommandPending = true;
            m_keyboardResponseReady = false;
            m_shiftCycles = keyboardByteCycles;
        }
        if (m_shiftRegisterWrite && (m_registers[auxiliaryControl] & 0x10) != 0) m_shiftRegisterWrite(value);
        return;
    }
    if (index == timer2CounterHigh) {
        m_registers[index] = value;
        m_timer2Counter = (static_cast<int>(m_registers[timer2CounterHigh]) << 8) | m_registers[timer2CounterLow];
        m_timer2Running = true;
        m_registers[interruptFlag] &= static_cast<std::uint8_t>(~timer2InterruptBit);
        return;
    }

    if (index == registerAHandshake) {
        m_registers[registerA] = value;
    } else {
        m_registers[index] = value;
    }
    if (index == registerA || index == registerAHandshake || index == dataDirectionA) {
        notifyPortAChanged();
    } else if ((index == registerB || index == dataDirectionB) && m_portBChanged) {
        m_portBChanged(m_registers[registerB], m_registers[dataDirectionB]);
    }
}

void Via6522::tick(int cycles)
{
    if (cycles <= 0) {
        return;
    }

    if (m_automaticCa1Period > 0) {
        m_vblCycles -= cycles;
        while (m_vblCycles <= 0) {
            m_vblCycles += m_automaticCa1Period;
            m_registers[interruptFlag] |= vblInterruptBit;
        }
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
        } else {
            m_timer2Counter = 0;
            m_timer2Running = false;
            m_registers[timer2CounterLow] = 0;
            m_registers[timer2CounterHigh] = 0;
            m_registers[interruptFlag] |= timer2InterruptBit;
        }
    }

    if (m_shiftCycles > 0) {
        m_shiftCycles -= cycles;
        if (m_shiftCycles <= 0) {
            m_shiftCycles = 0;
            if ((m_registers[auxiliaryControl] & 0x10) == 0 && m_keyboardCommandPending) {
                if (m_keyboardCommand == 0x16) {
                    m_registers[shiftRegister] = 0x03;
                } else if (!m_keyboardTransitions.empty()) {
                    m_registers[shiftRegister] = m_keyboardTransitions.front();
                    m_keyboardTransitions.pop_front();
                } else {
                    m_registers[shiftRegister] = 0x7b;
                }
                m_keyboardResponseReady = true;
            }
            m_registers[interruptFlag] |= shiftRegisterInterruptBit;
        }
    }
}

void Via6522::setPortAChangedCallback(PortAChangedCallback callback)
{
    m_portAChanged = std::move(callback);
}

void Via6522::setPortBChangedCallback(PortBChangedCallback callback)
{
    m_portBChanged = std::move(callback);
    if (m_portBChanged) m_portBChanged(m_registers[registerB], m_registers[dataDirectionB]);
}

void Via6522::setShiftRegisterWriteCallback(ShiftRegisterWriteCallback callback)
{
    m_shiftRegisterWrite = std::move(callback);
}

void Via6522::externalShiftIn(std::uint8_t value)
{
    m_registers[shiftRegister] = value;
    m_registers[interruptFlag] |= shiftRegisterInterruptBit;
}

void Via6522::externalShiftOutComplete()
{
    m_registers[interruptFlag] |= shiftRegisterInterruptBit;
}

void Via6522::setPowerOnState(std::uint8_t portA, std::uint8_t portB, std::uint8_t ddrA, std::uint8_t ddrB)
{
    m_initialPortA = portA;
    m_initialPortB = portB;
    m_initialDdrA = ddrA;
    m_initialDdrB = ddrB;
}

void Via6522::setAutomaticCa1Period(int cycles)
{
    m_automaticCa1Period = std::max(0, cycles);
    m_vblCycles = m_automaticCa1Period;
}

void Via6522::setCa1(bool high)
{
    if (m_ca1 && !high) m_registers[interruptFlag] |= vblInterruptBit;
    m_ca1 = high;
}

void Via6522::setCb1(bool high)
{
    if (m_cb1 && !high) m_registers[interruptFlag] |= cb1InterruptBit;
    m_cb1 = high;
}

void Via6522::setCb2(bool high)
{
    if (m_cb2 && !high) m_registers[interruptFlag] |= cb2InterruptBit;
    m_cb2 = high;
}

std::uint8_t Via6522::portA() const
{
    const auto outputs = static_cast<std::uint8_t>(m_registers[registerA] & m_registers[dataDirectionA]);
    const auto inputs = static_cast<std::uint8_t>(m_portAInputs & ~m_registers[dataDirectionA]);
    return static_cast<std::uint8_t>(outputs | inputs);
}

void Via6522::setPortAInputBit(std::uint8_t bit, bool high)
{
    if (bit >= 8) return;
    const auto mask = static_cast<std::uint8_t>(1U << bit);
    if (high) m_portAInputs |= mask;
    else m_portAInputs &= static_cast<std::uint8_t>(~mask);
}

std::uint8_t Via6522::portB() const
{
    const auto outputs = static_cast<std::uint8_t>(m_registers[registerB] & m_registers[dataDirectionB]);
    const auto inputs = static_cast<std::uint8_t>(m_portBInputs & ~m_registers[dataDirectionB]);
    return static_cast<std::uint8_t>(outputs | inputs);
}

void Via6522::setPortBInputBit(std::uint8_t bit, bool high)
{
    if (bit >= 8) {
        return;
    }

    const auto mask = static_cast<std::uint8_t>(1U << bit);
    if (high) {
        m_portBInputs |= mask;
    } else {
        m_portBInputs &= static_cast<std::uint8_t>(~mask);
    }
}

void Via6522::queueKeyboardTransition(std::uint8_t keyCode, bool pressed)
{
    keyCode &= 0x3f;
    m_keyboardTransitions.push_back(static_cast<std::uint8_t>((keyCode << 1) | (pressed ? 0x00 : 0x80)));
    if (m_keyboardCommandPending && m_keyboardCommand == 0x10 && !m_keyboardResponseReady
        && (m_registers[auxiliaryControl] & 0x10) == 0) {
        m_shiftCycles = keyboardByteCycles;
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
        m_keyboardCommand,
        m_shiftCycles,
        m_keyboardTransitions.size(),
        m_keyboardCommandPending,
        m_keyboardResponseReady,
        m_registers[auxiliaryControl],
        m_registers[shiftRegister],
        portA(),
        portB(),
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
