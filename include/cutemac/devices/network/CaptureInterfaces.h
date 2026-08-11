#pragma once

#include <QString>
#include <QVector>

namespace cutemac::devices::network {

struct CaptureInterface {
    // Backend identity, stored in the profile: "en0", "\\Device\\NPF_{GUID}".
    QString deviceName;
    QString displayName;
    QString hardwareAddress;
    bool up = false;
    bool loopback = false;
    bool wireless = false;
    bool ethernet = false;
};

// Host interfaces the bridged backend could bind to, most plausible first.
// Empty when capture is unavailable; ask pcap::runtimeStatus() for the reason.
[[nodiscard]] QVector<CaptureInterface> availableCaptureInterfaces();

} // namespace cutemac::devices::network
