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
    static constexpr std::uint32_t acceleratorBase = 0x000a0000;
    static constexpr std::uint32_t acceleratorBytes = 0x100;

    enum class AcceleratorRegister : std::uint32_t {
        Signature = 0x00,
        Version = 0x04,
        Capabilities = 0x08,
        Status = 0x0c,
        CommandsSubmitted = 0x10,
        CommandsCompleted = 0x14,
        CommandsRejected = 0x18,
        FallbackOperations = 0x1c,
        BytesCopied = 0x20,
        LastCommand = 0x24,
        LastError = 0x28,
        GuestAdapter = 0x2c,
        GuestSystemVersion = 0x30,
        GuestAdapterVersion = 0x34,
        SourceOffset = 0x40,
        DestinationOffset = 0x44,
        StrideBytes = 0x48,
        WidthBytes = 0x4c,
        Height = 0x50,
        Flags = 0x54,
        Command = 0x58,
        Control = 0x5c,
    };

    enum : std::uint32_t {
        capabilityVramCopy = 1U << 0,
        statusEnabled = 1U << 0,
        statusBusy = 1U << 1,
        statusError = 1U << 2,
        statusGuestAttached = 1U << 3,
        copyBackward = 1U << 0,
        commandVramCopy = 1,
        controlResetStatistics = 1,
        controlGuestAttach = 2,
        controlGuestDetach = 3,
        controlRecordFallback = 4,
    };

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
    enum class AcceleratorError : std::uint32_t {
        None = 0,
        Disabled = 1,
        UnknownCommand = 2,
        InvalidDimensions = 3,
        OutOfRange = 4,
        InvalidStride = 5,
        InvalidFlags = 6,
    };

    [[nodiscard]] std::uint32_t readAcceleratorRegister(std::uint32_t offset) const;
    void writeAcceleratorRegister(std::uint32_t offset, std::uint32_t value);
    void executeCommand(std::uint32_t command);
    void executeVramCopy();
    void reject(AcceleratorError error);
    void resetStatistics();
    static void incrementSaturating(std::uint32_t& value, std::uint32_t amount = 1);

    bool m_acceleration;
    std::uint32_t m_vramBytes;
    CuteMacVideoCard m_compatibleCard;
    std::uint32_t m_status = 0;
    std::uint32_t m_commandsSubmitted = 0;
    std::uint32_t m_commandsCompleted = 0;
    std::uint32_t m_commandsRejected = 0;
    std::uint32_t m_fallbackOperations = 0;
    std::uint32_t m_bytesCopied = 0;
    std::uint32_t m_lastCommand = 0;
    std::uint32_t m_lastError = 0;
    std::uint32_t m_guestAdapter = 0;
    std::uint32_t m_guestSystemVersion = 0;
    std::uint32_t m_guestAdapterVersion = 0;
    std::uint32_t m_sourceOffset = 0;
    std::uint32_t m_destinationOffset = 0;
    std::uint32_t m_strideBytes = 0;
    std::uint32_t m_widthBytes = 0;
    std::uint32_t m_height = 0;
    std::uint32_t m_flags = 0;
};

} // namespace cutemac::devices::video::nubus
