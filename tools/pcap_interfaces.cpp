// Prints what the bridged backend can see on this host, so a capture problem
// can be diagnosed without launching the emulator. Given an interface name it
// also opens that interface the way the emulated card would.

#include "cutemac/devices/network/CaptureInterfaces.h"
#include "cutemac/devices/network/PcapEthernetBackend.h"
#include "cutemac/devices/network/PcapRuntime.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QThread>

#include <array>
#include <cstdint>
#include <iostream>

namespace {

std::array<std::uint8_t, 6> parseMac(const QString& text, bool& ok)
{
    std::array<std::uint8_t, 6> mac {};
    const auto parts = text.split(QLatin1Char(':'));
    ok = parts.size() == 6;
    if (!ok) return mac;
    for (int index = 0; index < 6; ++index) {
        bool octetOk = false;
        const auto value = parts.at(index).toUInt(&octetOk, 16);
        if (!octetOk || value > 255) {
            ok = false;
            return mac;
        }
        mac[static_cast<std::size_t>(index)] = static_cast<std::uint8_t>(value);
    }
    return mac;
}

int captureFrom(const QString& deviceName, const QString& stationAddress)
{
    cutemac::devices::network::PcapEthernetConfiguration configuration;
    configuration.interfaceName = deviceName;
    cutemac::devices::network::PcapEthernetBackend backend(configuration);

    if (!backend.connected()) {
        std::cout << "cannot capture on " << qPrintable(deviceName) << ": "
                  << qPrintable(backend.statusDetail()) << '\n';
        return 1;
    }
    if (stationAddress.isEmpty()) {
        // Nothing is filtered out until a guest driver programs the card, which
        // is what the emulated machine does through setStationAddress().
        backend.setPromiscuous(true);
        std::cout << "capturing everything on " << qPrintable(deviceName) << " for 3 seconds\n";
    } else {
        bool ok = false;
        const auto mac = parseMac(stationAddress, ok);
        if (!ok) {
            std::cout << "not a MAC address: " << qPrintable(stationAddress) << '\n';
            return 1;
        }
        backend.setStationAddress(mac);
        std::cout << "capturing traffic for " << qPrintable(stationAddress) << " on "
                  << qPrintable(deviceName) << " for 3 seconds\n";
    }
    QElapsedTimer timer;
    timer.start();
    int frames = 0;
    qsizetype bytes = 0;
    while (timer.elapsed() < 3000) {
        backend.poll();
        while (auto frame = backend.receiveFrame()) {
            ++frames;
            bytes += frame->size();
        }
        QThread::msleep(1);
    }
    std::cout << "captured " << frames << " frames, " << bytes << " bytes\n";
    if (!backend.connected()) {
        std::cout << "link went down: " << qPrintable(backend.statusDetail()) << '\n';
        return 1;
    }
    return frames > 0 ? 0 : 2;
}

} // namespace

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);

    const auto& runtime = cutemac::devices::network::pcap::runtimeStatus();
    if (!runtime.available) {
        std::cout << "capture unavailable: " << qPrintable(runtime.reason) << '\n';
        return 1;
    }
    std::cout << "capture library: " << qPrintable(runtime.version) << '\n';

    const auto arguments = app.arguments();
    if (arguments.size() > 1) {
        return captureFrom(arguments.at(1), arguments.size() > 2 ? arguments.at(2) : QString());
    }

    const auto interfaces = cutemac::devices::network::availableCaptureInterfaces();
    if (interfaces.isEmpty()) {
        std::cout << "no capture interfaces are visible\n";
        return 1;
    }
    for (const auto& entry : interfaces) {
        std::cout << (entry.ethernet && entry.up && !entry.wireless ? "  * " : "    ")
                  << qPrintable(entry.deviceName) << "  " << qPrintable(entry.displayName);
        if (!entry.hardwareAddress.isEmpty()) std::cout << "  [" << qPrintable(entry.hardwareAddress) << ']';
        if (entry.up) std::cout << " up";
        if (entry.ethernet) std::cout << " ethernet";
        if (entry.wireless) std::cout << " wireless";
        if (entry.loopback) std::cout << " loopback";
        std::cout << '\n';
    }
    std::cout << "\nLines marked * are wired and running, and are the ones bridging works on.\n"
              << "Pass an interface name to capture from it, and a MAC address to apply the same\n"
              << "filter the emulated card would install.\n";
    return 0;
}
