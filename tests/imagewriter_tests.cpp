#include <QFileInfo>
#include <QImage>
#include <QTemporaryDir>

#include <iostream>

#include "cutemac/devices/printer/ImageWriterII.h"
#include "cutemac/devices/scc/Z8530Scc.h"
#include "cutemac/storage/PngPageSink.h"

class LoopbackEndpoint final : public cutemac::devices::serial::SerialEndpoint {
public:
    void receiveByte(std::uint8_t value) override { transmitByte(static_cast<std::uint8_t>(value + 1)); }
};

int main()
{
    QTemporaryDir directory;
    cutemac::storage::PngPageSink sink(directory.path());
    int pages = 0;
    auto printer = std::make_shared<cutemac::devices::printer::ImageWriterII>([&](const auto& page) {
        ++pages;
        if (!sink.write(page)) pages = -100;
    });
    cutemac::devices::scc::Z8530Scc scc;
    using Channel = cutemac::devices::scc::Z8530Scc::Channel;
    scc.attachEndpoint(Channel::B, printer);
    const auto send = [&](std::uint8_t byte) { scc.writeData(Channel::B, byte); scc.tick(16); };
    send(0x1b); send('G');
    for (const auto byte : QByteArray("0002")) send(static_cast<std::uint8_t>(byte));
    send(0x81); send(0x42); send(0x0c);

    const QImage image(sink.lastOutputPath());
    if (pages != 1 || image.isNull() || image.width() != 1224 || image.height() != 1584
        || !QFileInfo::exists(directory.filePath(QStringLiteral("ImageWriter-000001.png")))) {
        std::cerr << "ImageWriter serial PNG test failed\n";
        return 1;
    }
    auto loopback = std::make_shared<LoopbackEndpoint>();
    scc.attachEndpoint(Channel::A, loopback);
    send(0); // still addresses channel B
    scc.writeData(Channel::A, 0x40); scc.tick(16);
    if ((scc.readControl(Channel::A) & 1U) == 0 || scc.readData(Channel::A) != 0x41) {
        std::cerr << "generic bidirectional serial endpoint test failed\n";
        return 1;
    }
    scc.writeControl(Channel::A, 1); // select RR1
    if ((scc.readControl(Channel::A) & 1U) == 0) {
        std::cerr << "SCC RR1 should report All Sent when the transmitter is idle\n";
        return 1;
    }
    return 0;
}
