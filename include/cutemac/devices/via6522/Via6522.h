#pragma once

#include <array>
#include <cstdint>
#include <functional>

namespace cutemac::devices::via6522 {

class Via6522 {
public:
    using PortAChangedCallback = std::function<void(std::uint8_t)>;

    void reset();

    [[nodiscard]] std::uint8_t readRegister(std::uint8_t index);
    void writeRegister(std::uint8_t index, std::uint8_t value);

    void setPortAChangedCallback(PortAChangedCallback callback);

    [[nodiscard]] std::uint8_t portA() const;
    [[nodiscard]] std::uint8_t portB() const;
    void setPortBInputBit(std::uint8_t bit, bool high);
    [[nodiscard]] bool overlayEnabled() const;

private:
    void notifyPortAChanged();
    [[nodiscard]] std::uint8_t interruptFlagRegister() const;

    std::array<std::uint8_t, 16> m_registers {};
    std::uint8_t m_interruptEnable = 0;
    PortAChangedCallback m_portAChanged;
};

} // namespace cutemac::devices::via6522
