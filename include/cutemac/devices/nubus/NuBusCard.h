#pragma once

#include <cstdint>
#include <functional>

#include <QString>

#include "cutemac/core/GuestPowerRequest.h"
#include "cutemac/devices/video/VideoFrame.h"

namespace cutemac::devices::nubus {

class NuBusCard {
public:
    using IrqCallback = std::function<void(bool)>;

    virtual ~NuBusCard() = default;

    [[nodiscard]] virtual QString id() const = 0;
    virtual void reset() = 0;
    virtual void tick(std::uint64_t cycles) = 0;
    [[nodiscard]] virtual std::uint8_t read8(std::uint32_t offset) = 0;
    virtual void write8(std::uint32_t offset, std::uint8_t value) = 0;
    virtual void write16(std::uint32_t offset, std::uint16_t value)
    {
        write8(offset, static_cast<std::uint8_t>(value >> 8));
        write8(offset + 1, static_cast<std::uint8_t>(value));
    }
    virtual void write32(std::uint32_t offset, std::uint32_t value)
    {
        write16(offset, static_cast<std::uint16_t>(value >> 16));
        write16(offset + 2, static_cast<std::uint16_t>(value));
    }
    [[nodiscard]] virtual video::VideoFrame videoFrame() const { return {}; }
    [[nodiscard]] virtual core::GuestPowerRequest takePowerRequest() { return core::GuestPowerRequest::None; }

    void setIrqCallback(IrqCallback callback) { m_irqCallback = std::move(callback); }

protected:
    void setIrq(bool asserted)
    {
        if (m_irqAsserted == asserted) {
            // NuBus sources may reassert a level that is still pending. The
            // Macintosh bus glue turns that into a fresh VIA CA1 edge.
            if (asserted && m_irqCallback) m_irqCallback(true);
            return;
        }
        m_irqAsserted = asserted;
        if (m_irqCallback) m_irqCallback(asserted);
    }

private:
    IrqCallback m_irqCallback;
    bool m_irqAsserted = false;
};

} // namespace cutemac::devices::nubus
