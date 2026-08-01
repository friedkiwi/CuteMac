#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <deque>

namespace cutemac::devices::via6522 {

class Via6522 {
public:
    struct DebugState {
        std::uint8_t interruptFlags = 0;
        std::uint8_t interruptEnable = 0;
        int timer1Counter = 0;
        bool timer1Running = false;
        int timer2Counter = 0;
        bool timer2Running = false;
        bool interruptActive = false;
        std::uint8_t keyboardCommand = 0;
        int keyboardCycles = 0;
        std::size_t keyboardQueueDepth = 0;
        bool keyboardCommandPending = false;
        bool keyboardResponseReady = false;
    };

    using PortAChangedCallback = std::function<void(std::uint8_t)>;

    void reset();

    [[nodiscard]] std::uint8_t readRegister(std::uint8_t index);
    void writeRegister(std::uint8_t index, std::uint8_t value);
    void tick(int cycles);

    void setPortAChangedCallback(PortAChangedCallback callback);

    [[nodiscard]] std::uint8_t portA() const;
    [[nodiscard]] std::uint8_t portB() const;
    void setPortBInputBit(std::uint8_t bit, bool high);
    void queueKeyboardTransition(std::uint8_t keyCode, bool pressed);
    [[nodiscard]] bool overlayEnabled() const;
    [[nodiscard]] bool interruptActive() const;
    [[nodiscard]] DebugState debugState() const;

private:
    void notifyPortAChanged();
    [[nodiscard]] std::uint8_t interruptFlagRegister() const;

    std::array<std::uint8_t, 16> m_registers {};
    std::uint8_t m_portBInputs = 0xff;
    std::uint8_t m_interruptEnable = 0;
    int m_timer1Counter = 0;
    int m_timer1Latch = 0;
    bool m_timer1Running = false;
    int m_timer2Counter = 0;
    bool m_timer2Running = false;
    int m_vblCycles = 0;
    std::deque<std::uint8_t> m_keyboardTransitions;
    std::uint8_t m_keyboardCommand = 0;
    int m_shiftCycles = 0;
    bool m_keyboardCommandPending = false;
    bool m_keyboardResponseReady = false;
    PortAChangedCallback m_portAChanged;
};

} // namespace cutemac::devices::via6522
