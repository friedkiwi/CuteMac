#pragma once

#include <QByteArray>
#include <QString>

#include <array>
#include <cstdint>
#include <optional>

namespace cutemac::devices::network {

// A source and sink of raw Ethernet frames for an emulated network card.
//
// Threading: every method is called on the emulation thread, `poll()` from the
// owning device's `tick()` once per scheduler quantum. No method may block —
// a backend that needs blocking I/O must do it on its own thread and hand
// frames over through a queue.
class PacketNetworkBackend {
public:
    virtual ~PacketNetworkBackend() = default;

    virtual void poll() = 0;

    // False when the frame was dropped: link down, or the backend is saturated.
    [[nodiscard]] virtual bool transmitFrame(const QByteArray& frame) = 0;

    [[nodiscard]] virtual std::optional<QByteArray> receiveFrame() = 0;

    // Station-address hints, so a backend that filters in the host (hardware or
    // BPF) stays aligned with what the guest driver programmed into the card.
    // Backends that see only frames meant for the guest may ignore both.
    virtual void setStationAddress(const std::array<std::uint8_t, 6>&) { }
    virtual void setPromiscuous(bool) { }

    virtual void close() = 0;
    [[nodiscard]] virtual bool connected() const = 0;

    // User-facing explanation of why the link is down. Empty while healthy.
    [[nodiscard]] virtual QString statusDetail() const { return {}; }
};

} // namespace cutemac::devices::network
