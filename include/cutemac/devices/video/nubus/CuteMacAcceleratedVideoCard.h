#pragma once

#include "cutemac/devices/nubus/NuBusCard.h"
#include "cutemac/devices/video/nubus/CuteMacVideoCard.h"

namespace cutemac::devices::video::nubus {

// Experimental accelerated variant. It deliberately delegates the complete
// working card today so accelerator MMIO and its Retro68 driver can evolve
// without changing CuteMacVideoCard.
class CuteMacAcceleratedVideoCard final : public devices::nubus::NuBusCard {
public:
    static constexpr std::uint32_t guestServicesBase = CuteMacVideoCard::guestServicesBase;
    static constexpr std::uint32_t guestServicesCommand = CuteMacVideoCard::guestServicesCommand;
    static constexpr std::uint32_t guestPointerBase = CuteMacVideoCard::guestPointerBase;

    CuteMacAcceleratedVideoCard(int width, int height, int depth, int vramMiB,
        bool acceleration = true, bool absolutePointer = true);

    [[nodiscard]] QString id() const override;
    void reset() override;
    void tick(std::uint64_t cycles) override;
    [[nodiscard]] std::uint8_t read8(std::uint32_t offset) override;
    void write8(std::uint32_t offset, std::uint8_t value) override;
    void write16(std::uint32_t offset, std::uint16_t value) override;
    void write32(std::uint32_t offset, std::uint32_t value) override;
    [[nodiscard]] VideoFrame videoFrame() const override;
    [[nodiscard]] core::GuestPowerRequest takePowerRequest() override;

    [[nodiscard]] const QByteArray& declarationRom() const;
    [[nodiscard]] bool accelerationEnabled() const { return m_acceleration; }
    [[nodiscard]] bool absolutePointerEnabled() const;
    void setHostPointerPosition(std::int16_t x, std::int16_t y);

private:
    bool m_acceleration;
    CuteMacVideoCard m_compatibleCard;
};

} // namespace cutemac::devices::video::nubus
