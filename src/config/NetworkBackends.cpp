#include "cutemac/config/NetworkBackends.h"

#include "cutemac/devices/network/PcapRuntime.h"

#ifndef CUTEMAC_HAS_LIBSLIRP
#define CUTEMAC_HAS_LIBSLIRP 0
#endif

#ifndef CUTEMAC_HAS_PCAP
#define CUTEMAC_HAS_PCAP 0
#endif

namespace cutemac::config {

const QVector<NetworkBackendDescriptor>& networkBackendDescriptors()
{
    static const QVector<NetworkBackendDescriptor> descriptors {
        {
            NetworkBackendType::None,
            QStringLiteral("none"),
            QStringLiteral("None"),
            QStringLiteral("The card is installed but its wire is unplugged."),
            false,
        },
        {
            NetworkBackendType::Slirp,
            QStringLiteral("slirp"),
            QStringLiteral("SLIRP (user-mode NAT)"),
            QStringLiteral("Outbound TCP/IP through the host, with no driver or privileges required. "
                           "The guest is not reachable from the local network."),
            false,
        },
        {
            NetworkBackendType::Pcap,
            QStringLiteral("pcap"),
            QStringLiteral("Bridged (host interface)"),
            QStringLiteral("The emulated Mac appears on the local network with its own MAC address. "
                           "Needs a wired Ethernet interface and capture permissions."),
            true,
        },
    };
    return descriptors;
}

const NetworkBackendDescriptor& networkBackendDescriptor(NetworkBackendType type)
{
    const auto& descriptors = networkBackendDescriptors();
    for (const auto& descriptor : descriptors) {
        if (descriptor.type == type) return descriptor;
    }
    return descriptors.first();
}

QString networkBackendName(NetworkBackendType type)
{
    return networkBackendDescriptor(type).tomlName;
}

NetworkBackendType networkBackendFromName(const QString& name)
{
    for (const auto& descriptor : networkBackendDescriptors()) {
        if (descriptor.tomlName == name) return descriptor.type;
    }
    return NetworkBackendType::None;
}

bool networkBackendSupported(NetworkBackendType type)
{
    switch (type) {
    case NetworkBackendType::None:
        return true;
    case NetworkBackendType::Slirp:
        return CUTEMAC_HAS_LIBSLIRP != 0;
    case NetworkBackendType::Pcap:
        return CUTEMAC_HAS_PCAP != 0;
    }
    return false;
}

NetworkBackendAvailability networkBackendAvailability(NetworkBackendType type)
{
    switch (type) {
    case NetworkBackendType::None:
        return { true, {} };
    case NetworkBackendType::Slirp:
        if (networkBackendSupported(type)) return { true, {} };
        return { false, QStringLiteral("This build has no SLIRP support.") };
    case NetworkBackendType::Pcap: {
        // Compiled in is not the same as usable: on Windows the capture library
        // belongs to a separately installed Npcap.
        const auto& runtime = devices::network::pcap::runtimeStatus();
        return { runtime.available, runtime.reason };
    }
    }
    return { false, QStringLiteral("Unknown network backend.") };
}

} // namespace cutemac::config
