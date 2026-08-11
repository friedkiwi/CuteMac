#include "cutemac/devices/network/CaptureInterfaces.h"

#include "cutemac/devices/network/PcapRuntime.h"

#include <QHash>
#include <QNetworkInterface>

#include <algorithm>

#ifndef CUTEMAC_HAS_PCAP
#define CUTEMAC_HAS_PCAP 0
#endif

#if CUTEMAC_HAS_PCAP
#include <pcap.h>
#endif

namespace cutemac::devices::network {

namespace {

// pcap and Qt name the same adapter differently. On Unix both report "en0"; on
// Windows pcap reports \Device\NPF_{GUID} while Qt reports the GUID. Reduce
// both to a comparable key.
QString interfaceKey(const QString& name)
{
    const auto open = name.indexOf(QLatin1Char('{'));
    const auto close = name.indexOf(QLatin1Char('}'), open + 1);
    if (open >= 0 && close > open) {
        return name.mid(open + 1, close - open - 1).toLower();
    }
    return name.toLower();
}

QHash<QString, QNetworkInterface> hostInterfacesByKey()
{
    QHash<QString, QNetworkInterface> byKey;
    for (const auto& iface : QNetworkInterface::allInterfaces()) {
        byKey.insert(interfaceKey(iface.name()), iface);
    }
    return byKey;
}

// Wired, running interfaces first: those are the ones bridging actually works
// on. Wireless and down interfaces stay listed but sink to the bottom.
bool moreUseful(const CaptureInterface& lhs, const CaptureInterface& rhs)
{
    const auto rank = [](const CaptureInterface& value) {
        if (value.loopback) return 3;
        if (value.wireless) return 2;
        if (!value.up) return 1;
        return 0;
    };
    if (rank(lhs) != rank(rhs)) return rank(lhs) < rank(rhs);
    return lhs.deviceName.compare(rhs.deviceName, Qt::CaseInsensitive) < 0;
}

} // namespace

QVector<CaptureInterface> availableCaptureInterfaces()
{
#if !CUTEMAC_HAS_PCAP
    return {};
#else
    if (!pcap::runtimeStatus().available) return {};

    char errorBuffer[PCAP_ERRBUF_SIZE] = {};
    pcap_if_t* devices = nullptr;
    if (pcap_findalldevs(&devices, errorBuffer) != 0 || devices == nullptr) {
        if (devices != nullptr) pcap_freealldevs(devices);
        return {};
    }

    const auto hostInterfaces = hostInterfacesByKey();
    QVector<CaptureInterface> result;
    for (const auto* device = devices; device != nullptr; device = device->next) {
        CaptureInterface entry;
        entry.deviceName = QString::fromLocal8Bit(device->name);
        entry.loopback = (device->flags & PCAP_IF_LOOPBACK) != 0;
        entry.wireless = (device->flags & PCAP_IF_WIRELESS) != 0;
        entry.up = (device->flags & PCAP_IF_UP) != 0 && (device->flags & PCAP_IF_RUNNING) != 0;

        const auto iface = hostInterfaces.value(interfaceKey(entry.deviceName));
        if (iface.isValid()) {
            entry.hardwareAddress = iface.hardwareAddress();
            entry.wireless = entry.wireless || iface.type() == QNetworkInterface::Wifi;
            entry.loopback = entry.loopback || iface.flags().testFlag(QNetworkInterface::IsLoopBack);
            entry.ethernet = iface.type() == QNetworkInterface::Ethernet;
            if (iface.humanReadableName() != iface.name()) entry.displayName = iface.humanReadableName();
        } else {
            // No Qt match: trust pcap's own flags, and its description, which
            // Npcap fills in with the adapter's friendly name.
            entry.ethernet = !entry.loopback && !entry.wireless;
        }
        if (entry.displayName.isEmpty() && device->description != nullptr) {
            entry.displayName = QString::fromLocal8Bit(device->description);
        }
        if (entry.displayName.isEmpty()) entry.displayName = entry.deviceName;

        result.append(entry);
    }
    pcap_freealldevs(devices);

    std::sort(result.begin(), result.end(), moreUseful);
    return result;
#endif
}

} // namespace cutemac::devices::network
