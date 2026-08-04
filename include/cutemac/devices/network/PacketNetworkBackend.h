#pragma once

#include <QByteArray>

#include <optional>

namespace cutemac::devices::network {

class PacketNetworkBackend {
public:
    virtual ~PacketNetworkBackend() = default;

    virtual void poll() = 0;
    virtual void transmitFrame(const QByteArray& frame) = 0;
    [[nodiscard]] virtual std::optional<QByteArray> receiveFrame() = 0;
    virtual void close() = 0;
    [[nodiscard]] virtual bool connected() const = 0;
};

} // namespace cutemac::devices::network
