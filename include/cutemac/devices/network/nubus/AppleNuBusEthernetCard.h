#pragma once

#include <array>
#include <cstdint>
#include <memory>

#include <QByteArray>
#include <QString>

#include "cutemac/devices/network/PacketNetworkBackend.h"
#include "cutemac/devices/nubus/NuBusCard.h"

namespace cutemac::devices::network::nubus {

class AppleNuBusEthernetCard final : public devices::nubus::NuBusCard {
public:
    static constexpr int declarationRomBytes = 16 * 1024;
    static constexpr int mappedDeclarationRomBytes = declarationRomBytes * 2;
    static constexpr int packetRamBytes = 64 * 1024;

    explicit AppleNuBusEthernetCard(QString macAddress = {},
        std::unique_ptr<network::PacketNetworkBackend> backend = {});
    ~AppleNuBusEthernetCard() override;

    [[nodiscard]] QString id() const override;
    [[nodiscard]] bool loadDeclarationRom(const QString& path);
    [[nodiscard]] bool declarationRomLoaded() const { return m_declarationRom.size() == mappedDeclarationRomBytes; }
    void reset() override;
    void tick(std::uint64_t cycles) override;
    [[nodiscard]] std::uint8_t read8(std::uint32_t offset) override;
    [[nodiscard]] std::uint16_t read16(std::uint32_t offset) override;
    [[nodiscard]] std::uint32_t read32(std::uint32_t offset) override;
    void write8(std::uint32_t offset, std::uint8_t value) override;
    void write16(std::uint32_t offset, std::uint16_t value) override;
    void write32(std::uint32_t offset, std::uint32_t value) override;

    [[nodiscard]] std::uint8_t packetRamByte(std::uint16_t offset) const;

private:
    class Dp8390;

    [[nodiscard]] std::uint8_t readPacketRam(std::uint32_t offset) const;
    void writePacketRam(std::uint32_t offset, std::uint8_t value);
    [[nodiscard]] std::uint8_t readRegisterByte(std::uint32_t local);
    [[nodiscard]] std::uint16_t readRegisterWord(std::uint32_t local);
    void writeRegisterByte(std::uint32_t local, std::uint8_t value);
    void writeRegisterWord(std::uint32_t local, std::uint16_t value);
    [[nodiscard]] bool transmitFrame(const QByteArray& frame);
    [[nodiscard]] std::array<std::uint8_t, 6> macAddressBytes() const;

    QString m_macAddress;
    std::unique_ptr<network::PacketNetworkBackend> m_backend;
    std::unique_ptr<Dp8390> m_dp8390;
    QByteArray m_declarationRom;
    QByteArray m_packetRam;
};

} // namespace cutemac::devices::network::nubus
