#include <QCoreApplication>
#include <QTcpServer>
#include <QTcpSocket>

#include <algorithm>
#include <chrono>
#include <iostream>
#include <thread>

#include "cutemac/config/Configuration.h"
#include "cutemac/devices/modem/HayesModem.h"
#include "cutemac/devices/modem/NullModem.h"
#include "cutemac/devices/scc/Z8530Scc.h"

namespace {

constexpr std::uint8_t slipEnd = 0xc0;
constexpr std::uint8_t slipEsc = 0xdb;
constexpr std::uint8_t slipEscEnd = 0xdc;
constexpr std::uint8_t slipEscEsc = 0xdd;
constexpr std::uint16_t pppProtocolIp = 0x0021;
constexpr std::uint16_t pppProtocolLcp = 0xc021;
constexpr std::uint16_t pppProtocolPap = 0xc023;
constexpr std::uint16_t pppProtocolIpcp = 0x8021;

bool expect(bool condition, const char* message)
{
    if (!condition) std::cerr << "FAIL: " << message << '\n';
    return condition;
}

std::uint16_t readBe16(const unsigned char* bytes)
{
    return static_cast<std::uint16_t>((static_cast<std::uint16_t>(bytes[0]) << 8) | bytes[1]);
}

void writeBe16(unsigned char* bytes, std::uint16_t value)
{
    bytes[0] = static_cast<unsigned char>(value >> 8);
    bytes[1] = static_cast<unsigned char>(value);
}

std::uint16_t checksum(const unsigned char* data, qsizetype length)
{
    std::uint32_t sum = 0;
    for (qsizetype index = 0; index + 1 < length; index += 2) sum += readBe16(data + index);
    if ((length & 1) != 0) sum += static_cast<std::uint32_t>(data[length - 1]) << 8;
    while ((sum >> 16) != 0) sum = (sum & 0xffff) + (sum >> 16);
    return static_cast<std::uint16_t>(~sum);
}

std::uint16_t pppFcs(const unsigned char* data, qsizetype length)
{
    std::uint16_t fcs = 0xffff;
    for (qsizetype index = 0; index < length; ++index) {
        fcs ^= data[index];
        for (int bit = 0; bit < 8; ++bit) fcs = static_cast<std::uint16_t>((fcs & 1) != 0 ? (fcs >> 1) ^ 0x8408 : fcs >> 1);
    }
    return fcs;
}

QByteArray encodeSlip(const QByteArray& packet)
{
    QByteArray wire;
    wire.append(static_cast<char>(slipEnd));
    for (const auto raw : packet) {
        const auto byte = static_cast<std::uint8_t>(raw);
        if (byte == slipEnd) {
            wire.append(static_cast<char>(slipEsc));
            wire.append(static_cast<char>(slipEscEnd));
        } else if (byte == slipEsc) {
            wire.append(static_cast<char>(slipEsc));
            wire.append(static_cast<char>(slipEscEsc));
        } else {
            wire.append(raw);
        }
    }
    wire.append(static_cast<char>(slipEnd));
    return wire;
}

QByteArray firstSlipFrame(const QByteArray& wire)
{
    QByteArray frame;
    bool inFrame = false;
    bool escape = false;
    for (const auto raw : wire) {
        auto byte = static_cast<std::uint8_t>(raw);
        if (byte == slipEnd) {
            if (inFrame && !frame.isEmpty()) return frame;
            inFrame = true;
            frame.clear();
            escape = false;
            continue;
        }
        if (!inFrame) continue;
        if (escape) {
            escape = false;
            if (byte == slipEscEnd) byte = slipEnd;
            else if (byte == slipEscEsc) byte = slipEsc;
        } else if (byte == slipEsc) {
            escape = true;
            continue;
        }
        frame.append(static_cast<char>(byte));
    }
    return {};
}

QByteArray buildGatewayEchoRequest()
{
    QByteArray packet(32, '\0');
    auto* bytes = reinterpret_cast<unsigned char*>(packet.data());
    bytes[0] = 0x45;
    writeBe16(bytes + 2, packet.size());
    writeBe16(bytes + 4, 0x1234);
    bytes[8] = 64;
    bytes[9] = 1;
    bytes[12] = 172;
    bytes[13] = 16;
    bytes[14] = 0;
    bytes[15] = 2;
    bytes[16] = 172;
    bytes[17] = 16;
    bytes[18] = 0;
    bytes[19] = 1;
    bytes[20] = 8;
    bytes[21] = 0;
    writeBe16(bytes + 24, 0x4567);
    writeBe16(bytes + 26, 1);
    bytes[28] = 't';
    bytes[29] = 'e';
    bytes[30] = 's';
    bytes[31] = 't';
    writeBe16(bytes + 10, checksum(bytes, 20));
    writeBe16(bytes + 22, checksum(bytes + 20, packet.size() - 20));
    return packet;
}

QByteArray encodePpp(std::uint16_t protocol, const QByteArray& payload)
{
    QByteArray body;
    body.append(static_cast<char>(0xff));
    body.append(static_cast<char>(0x03));
    body.append(static_cast<char>(protocol >> 8));
    body.append(static_cast<char>(protocol));
    body.append(payload);
    const auto fcs = static_cast<std::uint16_t>(pppFcs(reinterpret_cast<const unsigned char*>(body.constData()), body.size()) ^ 0xffff);
    body.append(static_cast<char>(fcs));
    body.append(static_cast<char>(fcs >> 8));
    QByteArray frame;
    frame.append(static_cast<char>(0x7e));
    for (const auto raw : body) {
        const auto byte = static_cast<std::uint8_t>(raw);
        if (byte == 0x7e || byte == 0x7d || byte < 0x20) {
            frame.append(static_cast<char>(0x7d));
            frame.append(static_cast<char>(byte ^ 0x20));
        } else {
            frame.append(raw);
        }
    }
    frame.append(static_cast<char>(0x7e));
    return frame;
}

QByteArray pppControl(std::uint8_t code, std::uint8_t id, const QByteArray& data)
{
    QByteArray packet;
    packet.append(static_cast<char>(code));
    packet.append(static_cast<char>(id));
    const auto length = static_cast<std::uint16_t>(data.size() + 4);
    packet.append(static_cast<char>(length >> 8));
    packet.append(static_cast<char>(length));
    packet.append(data);
    return packet;
}

struct PppFrame {
    std::uint16_t protocol = 0;
    QByteArray payload;
};

QVector<PppFrame> pppFrames(const QByteArray& wire)
{
    QVector<PppFrame> frames;
    QByteArray raw;
    bool inFrame = false;
    bool escape = false;
    auto finish = [&]() {
        if (raw.size() < 6) return;
        const auto* bytes = reinterpret_cast<const unsigned char*>(raw.constData());
        if (pppFcs(bytes, raw.size()) != 0xf0b8) return;
        qsizetype offset = 0;
        if (bytes[0] == 0xff && bytes[1] == 0x03) offset = 2;
        std::uint16_t protocol = 0;
        if ((bytes[offset] & 1) != 0) protocol = bytes[offset++];
        else {
            protocol = readBe16(bytes + offset);
            offset += 2;
        }
        frames.append({ protocol, raw.mid(offset, raw.size() - offset - 2) });
    };
    for (const auto rawByte : wire) {
        auto byte = static_cast<std::uint8_t>(rawByte);
        if (byte == 0x7e) {
            if (inFrame && !raw.isEmpty()) finish();
            inFrame = true;
            raw.clear();
            escape = false;
            continue;
        }
        if (!inFrame) continue;
        if (escape) {
            byte ^= 0x20;
            escape = false;
        } else if (byte == 0x7d) {
            escape = true;
            continue;
        }
        raw.append(static_cast<char>(byte));
    }
    return frames;
}

QByteArray drain(cutemac::devices::modem::HayesModem& modem)
{
    QByteArray bytes;
    modem.setTransmitHandler([&](std::uint8_t value) { bytes.append(static_cast<char>(value)); });
    for (int i = 0; i < 50; ++i) modem.poll();
    return bytes;
}

QByteArray writeCommand(cutemac::devices::modem::HayesModem& modem, const QByteArray& command)
{
    for (const auto byte : command) modem.receiveByte(static_cast<std::uint8_t>(byte));
    return drain(modem);
}

QByteArray waitForOutput(cutemac::devices::modem::HayesModem& modem, int iterations = 200)
{
    QByteArray bytes;
    modem.setTransmitHandler([&](std::uint8_t value) { bytes.append(static_cast<char>(value)); });
    for (int i = 0; i < iterations && bytes.isEmpty(); ++i) {
        modem.poll();
        QCoreApplication::processEvents();
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return bytes;
}

template<typename Endpoint>
QByteArray waitForEndpointOutput(Endpoint& endpoint, int iterations = 200)
{
    QByteArray bytes;
    endpoint.setTransmitHandler([&](std::uint8_t value) { bytes.append(static_cast<char>(value)); });
    for (int i = 0; i < iterations && bytes.isEmpty(); ++i) {
        endpoint.poll();
        QCoreApplication::processEvents();
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return bytes;
}

} // namespace

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    bool ok = true;

    cutemac::config::SerialDeviceConfiguration config;
    config.channel = 0;
    config.type = cutemac::config::SerialDeviceType::HayesModem;
    config.phonebook = cutemac::config::defaultSerialModemPhonebook();
    cutemac::devices::modem::HayesModem modem(config);

    ok &= expect(writeCommand(modem, "AT\r").contains("OK"), "basic AT command must return OK");
    auto noCarrier = writeCommand(modem, "ATDT1000\r");
#if CUTEMAC_HAS_LIBSLIRP
    ok &= expect(noCarrier.contains("CONNECT"), "default phone number 1000 must connect to SLIP when libslirp is available");
    const auto request = encodeSlip(buildGatewayEchoRequest());
    for (const auto byte : request) modem.receiveByte(static_cast<std::uint8_t>(byte));
    const auto replyWire = waitForOutput(modem, 200);
    const auto reply = firstSlipFrame(replyWire);
    ok &= expect(reply.size() == 32 && static_cast<std::uint8_t>(reply[20]) == 0
            && static_cast<std::uint8_t>(reply[12]) == 172
            && static_cast<std::uint8_t>(reply[15]) == 1
            && static_cast<std::uint8_t>(reply[19]) == 2,
        "SLIP/libslirp modem must answer ICMP echo for the configured gateway IP");

    cutemac::devices::modem::HayesModem pppModem(config);
    ok &= expect(writeCommand(pppModem, "ATDT1001\r").contains("CONNECT"),
        "default phone number 1001 must connect to PPP when libslirp is available");
    const auto lcpRequest = encodePpp(pppProtocolLcp, pppControl(1, 1, {}));
    for (const auto byte : lcpRequest) pppModem.receiveByte(static_cast<std::uint8_t>(byte));
    auto pppReply = pppFrames(waitForOutput(pppModem));
    ok &= expect(!pppReply.isEmpty() && pppReply.first().protocol == pppProtocolLcp
            && static_cast<std::uint8_t>(pppReply.first().payload[0]) == 2,
        "PPP must ACK LCP Configure-Request");
    QByteArray papData;
    papData.append(static_cast<char>(0));
    papData.append(static_cast<char>(0));
    for (const auto byte : encodePpp(pppProtocolPap, pppControl(1, 2, papData))) pppModem.receiveByte(static_cast<std::uint8_t>(byte));
    pppReply = pppFrames(waitForOutput(pppModem));
    ok &= expect(!pppReply.isEmpty() && pppReply.first().protocol == pppProtocolPap
            && static_cast<std::uint8_t>(pppReply.first().payload[0]) == 2,
        "PPP PAP must accept blank authentication");
    QByteArray ipcpZero;
    ipcpZero.append(static_cast<char>(3));
    ipcpZero.append(static_cast<char>(6));
    ipcpZero.append(QByteArray(4, '\0'));
    ipcpZero.append(static_cast<char>(129));
    ipcpZero.append(static_cast<char>(6));
    ipcpZero.append(QByteArray(4, '\0'));
    for (const auto byte : encodePpp(pppProtocolIpcp, pppControl(1, 3, ipcpZero))) pppModem.receiveByte(static_cast<std::uint8_t>(byte));
    pppReply = pppFrames(waitForOutput(pppModem));
    ok &= expect(!pppReply.isEmpty() && pppReply.first().protocol == pppProtocolIpcp
            && static_cast<std::uint8_t>(pppReply.first().payload[0]) == 3
            && pppReply.first().payload.contains(QByteArray::fromHex("0306ac100002"))
            && pppReply.first().payload.contains(QByteArray::fromHex("8106ac100001")),
        "PPP IPCP must NAK zero IP settings with guest IP and DNS");
    QByteArray ipcpExact = QByteArray::fromHex("0306ac1000028106ac100001");
    for (const auto byte : encodePpp(pppProtocolIpcp, pppControl(1, 4, ipcpExact))) pppModem.receiveByte(static_cast<std::uint8_t>(byte));
    pppReply = pppFrames(waitForOutput(pppModem));
    ok &= expect(!pppReply.isEmpty() && pppReply.first().protocol == pppProtocolIpcp
            && static_cast<std::uint8_t>(pppReply.first().payload[0]) == 2,
        "PPP IPCP must ACK requested assigned IP settings");
    for (const auto byte : encodePpp(pppProtocolIp, buildGatewayEchoRequest())) pppModem.receiveByte(static_cast<std::uint8_t>(byte));
    pppReply = pppFrames(waitForOutput(pppModem));
    auto ipReply = std::find_if(pppReply.cbegin(), pppReply.cend(), [](const auto& frame) { return frame.protocol == pppProtocolIp; });
    ok &= expect(ipReply != pppReply.cend() && ipReply->payload.size() == 32
            && static_cast<std::uint8_t>(ipReply->payload[20]) == 0,
        "PPP must carry IPv4 packets through the same gateway responder");
#else
    ok &= expect(noCarrier.contains("NO CARRIER"), "default phone number 1000 must fail cleanly when libslirp is unavailable");
#endif

    QTcpServer server;
    ok &= expect(server.listen(QHostAddress::LocalHost, 0), "local TCP server must listen");
    cutemac::config::SerialDeviceConfiguration tcpConfig;
    tcpConfig.channel = 0;
    tcpConfig.type = cutemac::config::SerialDeviceType::HayesModem;
    tcpConfig.directTcpDialing = true;
    cutemac::devices::modem::HayesModem tcpModem(tcpConfig);
    const auto dial = QByteArray("ATDT127.0.0.1:") + QByteArray::number(server.serverPort()) + "\r";
    for (const auto byte : dial) tcpModem.receiveByte(static_cast<std::uint8_t>(byte));
    ok &= expect(server.waitForNewConnection(3000), "direct TCP dialing must connect to host:port");
    auto* socket = server.nextPendingConnection();
    ok &= expect(socket != nullptr, "server must accept modem connection");
    ok &= expect(waitForOutput(tcpModem).contains("CONNECT"), "direct TCP dialing must report CONNECT");
    tcpModem.receiveByte('A');
    ok &= expect(socket->waitForReadyRead(3000) && socket->readAll() == QByteArray("A"), "online modem bytes must reach TCP peer");
    socket->write("B");
    ok &= expect(socket->waitForBytesWritten(3000), "TCP peer write failed");
    ok &= expect(waitForOutput(tcpModem).contains("B"), "TCP peer bytes must reach modem serial output");

    delete socket;

    QTcpServer listenerPeer;
    ok &= expect(listenerPeer.listen(QHostAddress::LocalHost, 0), "null-modem dial target must listen");
    cutemac::config::SerialDeviceConfiguration dialNullConfig;
    dialNullConfig.type = cutemac::config::SerialDeviceType::NullModem;
    dialNullConfig.tcpMode = cutemac::config::SerialTcpMode::Dial;
    dialNullConfig.tcpHost = QStringLiteral("127.0.0.1");
    dialNullConfig.tcpPort = listenerPeer.serverPort();
    cutemac::devices::modem::NullModem dialNull(dialNullConfig);
    ok &= expect(listenerPeer.waitForNewConnection(3000), "null-modem dial mode must connect to TCP host");
    auto* dialPeer = listenerPeer.nextPendingConnection();
    ok &= expect(dialPeer != nullptr, "null-modem dial peer accepted");
    dialNull.receiveByte('D');
    ok &= expect(dialPeer->waitForReadyRead(3000) && dialPeer->readAll() == QByteArray("D"),
        "null-modem dial mode must send serial bytes to TCP peer");
    dialPeer->write("d");
    ok &= expect(dialPeer->waitForBytesWritten(3000), "null-modem dial peer write failed");
    ok &= expect(waitForEndpointOutput(dialNull).contains("d"),
        "null-modem dial mode must deliver TCP bytes to serial output");
    delete dialPeer;

    QTcpServer portProbe;
    ok &= expect(portProbe.listen(QHostAddress::LocalHost, 0), "null-modem listen port probe failed");
    const int listenPort = static_cast<int>(portProbe.serverPort());
    portProbe.close();
    cutemac::config::SerialDeviceConfiguration listenNullConfig;
    listenNullConfig.type = cutemac::config::SerialDeviceType::NullModem;
    listenNullConfig.tcpMode = cutemac::config::SerialTcpMode::Listen;
    listenNullConfig.tcpHost = QStringLiteral("127.0.0.1");
    listenNullConfig.tcpPort = listenPort;
    cutemac::devices::modem::NullModem listenNull(listenNullConfig);
    QTcpSocket listenPeer;
    for (int tries = 0; tries < 100 && listenPeer.state() != QAbstractSocket::ConnectedState; ++tries) {
        listenPeer.connectToHost(QHostAddress::LocalHost, static_cast<quint16>(listenPort));
        if (listenPeer.waitForConnected(20)) break;
        listenPeer.abort();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    ok &= expect(listenPeer.state() == QAbstractSocket::ConnectedState, "null-modem listen mode must accept TCP client");
    listenNull.receiveByte('L');
    ok &= expect(listenPeer.waitForReadyRead(3000) && listenPeer.readAll() == QByteArray("L"),
        "null-modem listen mode must send serial bytes to TCP client");
    listenPeer.write("l");
    ok &= expect(listenPeer.waitForBytesWritten(3000), "null-modem listen peer write failed");
    ok &= expect(waitForEndpointOutput(listenNull).contains("l"),
        "null-modem listen mode must deliver TCP bytes to serial output");

    // Exercise the same complete path used by a Macintosh debugger console,
    // rather than only calling the endpoint directly.
    QTcpServer sccPortProbe;
    ok &= expect(sccPortProbe.listen(QHostAddress::LocalHost, 0), "SCC null-modem port probe failed");
    const int sccPort = static_cast<int>(sccPortProbe.serverPort());
    sccPortProbe.close();
    auto sccNullConfig = listenNullConfig;
    sccNullConfig.tcpPort = sccPort;
    auto sccNull = std::make_shared<cutemac::devices::modem::NullModem>(sccNullConfig);
    cutemac::devices::scc::Z8530Scc scc;
    using SccChannel = cutemac::devices::scc::Z8530Scc::Channel;
    scc.attachEndpoint(SccChannel::A, sccNull);
    QTcpSocket sccPeer;
    for (int tries = 0; tries < 100 && sccPeer.state() != QAbstractSocket::ConnectedState; ++tries) {
        sccPeer.connectToHost(QHostAddress::LocalHost, static_cast<quint16>(sccPort));
        if (sccPeer.waitForConnected(20)) break;
        sccPeer.abort();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    ok &= expect(sccPeer.state() == QAbstractSocket::ConnectedState,
        "SCC null-modem path must accept TCP client");
    scc.writeControl(SccChannel::A, 0x04); // WR4
    scc.writeControl(SccChannel::A, 0x44); // asynchronous x16 clock mode
    scc.writeData(SccChannel::A, 'S');
    scc.tick(16);
    ok &= expect(sccPeer.waitForReadyRead(3000) && sccPeer.readAll() == QByteArray("S"),
        "SCC transmit byte must reach null-modem TCP client");
    sccPeer.write("s");
    ok &= expect(sccPeer.waitForBytesWritten(3000), "SCC null-modem TCP client write failed");
    for (int tries = 0; tries < 100 && (scc.readControl(SccChannel::A) & 0x01) == 0; ++tries) {
        scc.tick(1);
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    ok &= expect((scc.readControl(SccChannel::A) & 0x01) != 0 && scc.readData(SccChannel::A) == 's',
        "null-modem TCP byte must reach SCC receiver");

    return ok ? 0 : 1;
}
