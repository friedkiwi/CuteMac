#pragma once

#include <QString>
#include <QVector>

namespace cutemac::config {

enum class NetworkBackendType {
    None,
    Slirp,
    Pcap,
};

struct NetworkBackendDescriptor {
    NetworkBackendType type = NetworkBackendType::None;
    QString tomlName;
    QString displayName;
    QString summary;
    // Pcap needs a host interface name alongside the backend selection.
    bool requiresInterface = false;
};

// Whether the backend can be used right now on this host. Compiled-in backends
// can still be unavailable: Npcap may not be installed, or the capture device
// may be barred by permissions. `reason` is user-facing and is empty only when
// `available` is true.
struct NetworkBackendAvailability {
    bool available = false;
    QString reason;
};

[[nodiscard]] const QVector<NetworkBackendDescriptor>& networkBackendDescriptors();
[[nodiscard]] const NetworkBackendDescriptor& networkBackendDescriptor(NetworkBackendType type);
[[nodiscard]] QString networkBackendName(NetworkBackendType type);
[[nodiscard]] NetworkBackendType networkBackendFromName(const QString& name);

// Compiled-in support. Profile validity is gated on this rather than on
// availability, so a profile written on a host with Npcap installed still loads
// and saves on a host without it; the backend then reports why it is idle.
[[nodiscard]] bool networkBackendSupported(NetworkBackendType type);
[[nodiscard]] NetworkBackendAvailability networkBackendAvailability(NetworkBackendType type);

} // namespace cutemac::config
