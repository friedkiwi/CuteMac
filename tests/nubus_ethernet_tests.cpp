#include "cutemac/devices/network/nubus/AppleNuBusEthernetCard.h"
#include "cutemac/devices/network/PacketNetworkBackend.h"
#include "cutemac/devices/nubus/NuBusBus.h"

#include <QFile>
#include <QTemporaryDir>

#include <array>
#include <deque>
#include <iostream>
#include <optional>

namespace {

bool expect(bool condition, const char* message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
    }
    return condition;
}

class FakeBackend final : public cutemac::devices::network::PacketNetworkBackend {
public:
    void poll() override { ++polls; }
    bool transmitFrame(const QByteArray& frame) override
    {
        transmitted.push_back(frame);
        return connectedValue;
    }
    std::optional<QByteArray> receiveFrame() override
    {
        if (received.empty()) return std::nullopt;
        auto frame = std::move(received.front());
        received.pop_front();
        return frame;
    }
    void close() override
    {
        connectedValue = false;
        received.clear();
    }
    bool connected() const override { return connectedValue; }
    void setStationAddress(const std::array<std::uint8_t, 6>& mac) override
    {
        stationAddress = mac;
        ++stationAddressUpdates;
    }
    void setPromiscuous(bool enabled) override { promiscuous = enabled; }

    bool connectedValue = true;
    std::array<std::uint8_t, 6> stationAddress {};
    int stationAddressUpdates = 0;
    bool promiscuous = false;
    int polls = 0;
    QVector<QByteArray> transmitted;
    std::deque<QByteArray> received;
};

std::uint32_t regAddress(std::uint8_t reg)
{
    return 0x000e0000U + (0x0fU - reg) * 4U;
}

QByteArray ethernetFrame()
{
    QByteArray frame(60, 0);
    auto* bytes = reinterpret_cast<std::uint8_t*>(frame.data());
    for (int index = 0; index < 6; ++index) bytes[index] = 0xff;
    bytes[6] = 0x02;
    bytes[7] = 0x00;
    bytes[8] = 0x1b;
    bytes[9] = 0x00;
    bytes[10] = 0x00;
    bytes[11] = 0x09;
    bytes[12] = 0x08;
    bytes[13] = 0x00;
    bytes[14] = 0x45;
    return frame;
}

} // namespace

int main()
{
    using cutemac::devices::network::nubus::AppleNuBusEthernetCard;

    bool ok = true;
    QTemporaryDir directory;
    const auto romPath = directory.filePath(QStringLiteral("aenet1"));
    QFile rom(romPath);
    ok &= expect(rom.open(QIODevice::WriteOnly), "Apple Ethernet ROM fixture open");
    QByteArray romBytes(AppleNuBusEthernetCard::declarationRomBytes, 0);
    romBytes[0] = 0x12;
    romBytes[1] = 0x34;
    romBytes[AppleNuBusEthernetCard::declarationRomBytes - 1] = static_cast<char>(0xa5);
    ok &= expect(rom.write(romBytes) == AppleNuBusEthernetCard::declarationRomBytes, "Apple Ethernet ROM fixture write");
    rom.close();

    auto backend = std::make_unique<FakeBackend>();
    auto* backendPtr = backend.get();
    AppleNuBusEthernetCard card(QStringLiteral("02:00:1b:00:00:09"), std::move(backend));
    ok &= expect(card.id() == QStringLiteral("nubus-network-apple-ethernet"), "Apple Ethernet card identity");
    ok &= expect(card.loadDeclarationRom(romPath), "Apple Ethernet card must accept the aenet1 lane-0/2 ROM");
    ok &= expect(card.read8(0x00ff8000) == 0x12
            && card.read8(0x00ff8001) == 0xff
            && card.read8(0x00ff8002) == 0x34
            && card.read8(0x00ff8003) == 0xff,
        "Apple Ethernet declaration ROM must expand onto NuBus byte lanes 0 and 2");

    card.write8(0x000d1234, 0x5a);
    ok &= expect(card.read8(0x000d1234) == 0x5a && card.packetRamByte(0x1234) == 0x5a,
        "Apple Ethernet packet RAM must be CPU-visible");

    card.write8(regAddress(0x00), 0x22);
    card.write8(regAddress(0x0e), 0x01);
    card.write8(regAddress(0x08), 0x20);
    card.write8(regAddress(0x09), 0x00);
    card.write8(regAddress(0x0a), 0x02);
    card.write8(regAddress(0x0b), 0x00);
    card.write8(regAddress(0x00), 0x12);
    card.write16(0x000e0000, 0x1234);
    ok &= expect(card.packetRamByte(0x20) == 0x34 && card.packetRamByte(0x21) == 0x12,
        "Apple Ethernet DP8390 remote DMA word writes must target packet RAM");

    const auto frame = ethernetFrame();
    for (qsizetype index = 0; index < frame.size(); ++index) {
        card.write8(0x000d4000U + static_cast<std::uint32_t>(index),
            static_cast<std::uint8_t>(frame[index]));
    }
    card.write8(regAddress(0x04), 0x40);
    card.write8(regAddress(0x05), static_cast<std::uint8_t>(frame.size()));
    card.write8(regAddress(0x06), 0x00);
    card.write8(regAddress(0x00), 0x26);
    ok &= expect(backendPtr->transmitted.size() == 1 && backendPtr->transmitted.first() == frame,
        "Apple Ethernet DP8390 transmit must deliver frames to the selected backend");

    card.write8(regAddress(0x01), 0x40);
    card.write8(regAddress(0x02), 0x60);
    card.write8(regAddress(0x03), 0x40);
    card.write8(regAddress(0x0c), 0x04);
    card.write8(regAddress(0x00), 0x62);
    card.write8(regAddress(0x07), 0x41);
    card.write8(regAddress(0x00), 0x22);
    backendPtr->received.push_back(frame);
    card.tick(1);
    ok &= expect(backendPtr->polls == 1
            && card.packetRamByte(0x4100) == 0x21
            && card.packetRamByte(0x4104) == 0xff,
        "Apple Ethernet DP8390 receive must ring-buffer backend frames");

    // The host-side backend can only filter for the guest if the card forwards
    // what the driver programmed into PAR and RCR.
    ok &= expect(backendPtr->stationAddress
            == std::array<std::uint8_t, 6> { 0x02, 0x00, 0x1b, 0x00, 0x00, 0x09 },
        "the card must publish its configured MAC address to the backend on reset");

    card.write8(regAddress(0x00), 0x62);
    const std::array<std::uint8_t, 6> programmedMac { 0x08, 0x00, 0x07, 0x11, 0x22, 0x33 };
    for (std::uint8_t index = 0; index < 6; ++index) {
        card.write8(regAddress(static_cast<std::uint8_t>(0x01 + index)), programmedMac[index]);
    }
    card.write8(regAddress(0x00), 0x22);
    ok &= expect(backendPtr->stationAddress == programmedMac,
        "PAR writes must reach the backend so it can filter for the guest");

    ok &= expect(!backendPtr->promiscuous, "a card filtering by address must not ask for promiscuous capture");
    card.write8(regAddress(0x0c), 0x14);
    ok &= expect(backendPtr->promiscuous, "RCR promiscuous mode must reach the backend");
    card.write8(regAddress(0x0c), 0x04);
    ok &= expect(!backendPtr->promiscuous, "leaving promiscuous mode must reach the backend");

    cutemac::devices::nubus::NuBusBus bus;
    auto first = std::make_shared<AppleNuBusEthernetCard>();
    auto second = std::make_shared<AppleNuBusEthernetCard>();
    ok &= expect(bus.install(9, first) && bus.install(10, second), "NuBus must accept multiple Ethernet cards");
    bus.write8(0x009d0000, 0x11);
    bus.write8(0x00ad0000, 0x22);
    ok &= expect(first->packetRamByte(0) == 0x11 && second->packetRamByte(0) == 0x22,
        "multiple NuBus Ethernet cards must retain independent packet RAM");

    return ok ? 0 : 1;
}
