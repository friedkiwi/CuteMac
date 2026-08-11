#include "cutemac/config/NetworkBackends.h"
#include "cutemac/devices/network/CaptureInterfaces.h"
#include "cutemac/devices/network/PcapEthernetBackend.h"
#include "cutemac/devices/network/PcapRuntime.h"

#include <QCoreApplication>

#include <array>
#include <iostream>

namespace {

bool expect(bool condition, const char* message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
    }
    return condition;
}

bool testFilterExpression()
{
    const std::array<std::uint8_t, 6> mac { 0x02, 0x00, 0x1b, 0x00, 0x00, 0x09 };
    const auto filter = cutemac::devices::network::captureFilterExpression(mac, false);
    bool ok = expect(filter == QStringLiteral("ether dst 02:00:1b:00:00:09 or ether broadcast or ether multicast"),
        "the capture filter must accept the guest MAC, broadcast and multicast");
    ok &= expect(filter.contains(QStringLiteral("02:00:1b:00:00:09")),
        "single-digit MAC octets must be zero padded");
    ok &= expect(cutemac::devices::network::captureFilterExpression(mac, true).isEmpty(),
        "a promiscuous guest must not be filtered");
    return ok;
}

bool testRuntPadding()
{
    QByteArray runt(14, '\x01');
    const auto padded = cutemac::devices::network::padToMinimumFrame(runt);
    bool ok = expect(padded.size() == 60, "runt frames must be padded to the 60-byte Ethernet minimum");
    ok &= expect(padded.left(14) == runt, "padding must not disturb the original bytes");
    ok &= expect(padded.at(59) == '\0', "padding must be zero filled");

    QByteArray full(60, '\x02');
    ok &= expect(cutemac::devices::network::padToMinimumFrame(full) == full,
        "frames at or above the minimum must pass through untouched");

    QByteArray jumbo(1514, '\x03');
    ok &= expect(cutemac::devices::network::padToMinimumFrame(jumbo).size() == 1514,
        "full-size frames must not be resized");
    return ok;
}

// The backend must fail politely rather than throwing or crashing, whether or
// not a capture library is present on the build machine.
bool testUnusableInterfaces()
{
    using cutemac::devices::network::PcapEthernetBackend;
    using cutemac::devices::network::PcapEthernetConfiguration;

    PcapEthernetConfiguration empty;
    PcapEthernetBackend unnamed(empty);
    bool ok = expect(!unnamed.connected(), "a backend with no interface must not report a link");
    ok &= expect(!unnamed.statusDetail().isEmpty(), "a backend with no interface must explain itself");
    ok &= expect(!unnamed.transmitFrame(QByteArray(60, '\0')),
        "transmitting on a down link must report the drop");
    ok &= expect(!unnamed.receiveFrame().has_value(), "a down link must yield no frames");
    unnamed.poll();
    unnamed.setStationAddress({ 0x02, 0x00, 0x1b, 0x00, 0x00, 0x09 });
    unnamed.setPromiscuous(true);
    unnamed.close();

    PcapEthernetConfiguration bogus;
    bogus.interfaceName = QStringLiteral("cutemac-no-such-interface0");
    PcapEthernetBackend missing(bogus);
    ok &= expect(!missing.connected(), "an absent interface must not report a link");
    ok &= expect(!missing.statusDetail().isEmpty(), "an absent interface must explain itself");
    if (cutemac::devices::network::pcap::runtimeStatus().available) {
        ok &= expect(missing.statusDetail().contains(QStringLiteral("cutemac-no-such-interface0")),
            "an absent interface must be named in the failure message");
    }
    return ok;
}

bool testAvailabilityReporting()
{
    const auto& runtime = cutemac::devices::network::pcap::runtimeStatus();
    bool ok = expect(runtime.available == runtime.reason.isEmpty(),
        "an unavailable capture runtime must explain why");
    ok &= expect(!runtime.available || !runtime.version.isEmpty(),
        "an available capture runtime must report its version");

    const auto availability =
        cutemac::config::networkBackendAvailability(cutemac::config::NetworkBackendType::Pcap);
    ok &= expect(availability.available == runtime.available,
        "the backend registry must mirror the capture runtime probe");
    ok &= expect(availability.available == availability.reason.isEmpty(),
        "an unavailable bridged backend must explain why");

    const auto& descriptor =
        cutemac::config::networkBackendDescriptor(cutemac::config::NetworkBackendType::Pcap);
    ok &= expect(descriptor.requiresInterface, "the bridged backend must require a host interface");
    ok &= expect(descriptor.tomlName == QStringLiteral("pcap"),
        "the bridged backend must serialize as \"pcap\"");
    return ok;
}

// Enumeration must be safe to call on any host, including one with no capture
// permissions and one where the feature was compiled out.
bool testInterfaceEnumeration()
{
    const auto interfaces = cutemac::devices::network::availableCaptureInterfaces();
    bool ok = true;
    if (!cutemac::devices::network::pcap::runtimeStatus().available) {
        return expect(interfaces.isEmpty(), "an unavailable capture runtime must enumerate nothing");
    }
    for (const auto& entry : interfaces) {
        ok &= expect(!entry.deviceName.isEmpty(), "every capture interface must have a device name");
        ok &= expect(!entry.displayName.isEmpty(), "every capture interface must have a display name");
    }
    for (int index = 1; index < interfaces.size(); ++index) {
        const auto& previous = interfaces[index - 1];
        const auto& current = interfaces[index];
        const auto rank = [](const cutemac::devices::network::CaptureInterface& value) {
            if (value.loopback) return 3;
            if (value.wireless) return 2;
            if (!value.up) return 1;
            return 0;
        };
        ok &= expect(rank(previous) <= rank(current),
            "wired running interfaces must sort ahead of wireless, down and loopback ones");
    }
    return ok;
}

} // namespace

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    bool ok = testFilterExpression();
    ok &= testRuntPadding();
    ok &= testUnusableInterfaces();
    ok &= testAvailabilityReporting();
    ok &= testInterfaceEnumeration();
    if (ok) std::cout << "pcap backend tests passed\n";
    return ok ? 0 : 1;
}
