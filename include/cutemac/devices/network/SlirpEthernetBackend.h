#pragma once

#include <array>
#include <cstdint>
#include <memory>

#include <QString>

#include "cutemac/devices/network/PacketNetworkBackend.h"

namespace cutemac::devices::network {

struct SlirpEthernetConfiguration {
    QString hostIp = QStringLiteral("172.16.0.1");
    QString guestIp = QStringLiteral("172.16.0.2");
    int mtu = 1500;
    bool dhcpEnabled = false;
};

class SlirpEthernetBackend final : public PacketNetworkBackend {
public:
    explicit SlirpEthernetBackend(SlirpEthernetConfiguration configuration);
    ~SlirpEthernetBackend() override;

    SlirpEthernetBackend(const SlirpEthernetBackend&) = delete;
    SlirpEthernetBackend& operator=(const SlirpEthernetBackend&) = delete;

    void poll() override;
    [[nodiscard]] bool transmitFrame(const QByteArray& frame) override;
    [[nodiscard]] std::optional<QByteArray> receiveFrame() override;
    void close() override;
    [[nodiscard]] bool connected() const override;
    [[nodiscard]] QString statusDetail() const override;

private:
    class Impl;
    std::unique_ptr<Impl> m_impl;
};

[[nodiscard]] std::array<std::uint8_t, 4> parseIpv4Address(
    const QString& text,
    const std::array<std::uint8_t, 4>& fallback);

} // namespace cutemac::devices::network
