#pragma once

#include <memory>

#include <QString>

#include "cutemac/devices/network/PacketNetworkBackend.h"

namespace cutemac::devices::network {

struct PcapEthernetConfiguration {
    // Host capture device, as named by availableCaptureInterfaces().
    QString interfaceName;
    // Bounds the work one poll() can do on the emulation thread.
    int maxFramesPerPoll = 32;
};

class PcapEthernetBackend final : public PacketNetworkBackend {
public:
    explicit PcapEthernetBackend(PcapEthernetConfiguration configuration);
    ~PcapEthernetBackend() override;

    PcapEthernetBackend(const PcapEthernetBackend&) = delete;
    PcapEthernetBackend& operator=(const PcapEthernetBackend&) = delete;

    void poll() override;
    [[nodiscard]] bool transmitFrame(const QByteArray& frame) override;
    [[nodiscard]] std::optional<QByteArray> receiveFrame() override;
    void setStationAddress(const std::array<std::uint8_t, 6>& mac) override;
    void setPromiscuous(bool enabled) override;
    void close() override;
    [[nodiscard]] bool connected() const override;
    [[nodiscard]] QString statusDetail() const override;

private:
    class Impl;
    std::unique_ptr<Impl> m_impl;
};

// The BPF expression that lets the guest's own traffic through and leaves the
// rest of the segment on the wire. Exposed for tests; an empty string means
// "capture everything", which is what a promiscuous guest asks for.
[[nodiscard]] QString captureFilterExpression(const std::array<std::uint8_t, 6>& mac, bool promiscuous);

// Frames shorter than the 60-byte Ethernet minimum are padded before injection;
// a DP8390 driver may hand over a runt and expect the wire to pad it.
[[nodiscard]] QByteArray padToMinimumFrame(const QByteArray& frame);

} // namespace cutemac::devices::network
