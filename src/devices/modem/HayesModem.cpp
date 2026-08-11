#include "cutemac/devices/modem/HayesModem.h"

#include "cutemac/devices/network/SlirpEthernetBackend.h"

#include <QTcpSocket>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>
#include <functional>

namespace cutemac::devices::modem {

namespace {

constexpr int maxPollBytes = 256;
constexpr std::uint8_t slipEnd = 0xc0;
constexpr std::uint8_t slipEsc = 0xdb;
constexpr std::uint8_t slipEscEnd = 0xdc;
constexpr std::uint8_t slipEscEsc = 0xdd;
constexpr int slipMaxPacketSize = 1006;
constexpr int ethernetHeaderSize = 14;
constexpr int arpPacketSize = 28;
constexpr std::uint16_t ethertypeIpv4 = 0x0800;
constexpr std::uint16_t ethertypeArp = 0x0806;
constexpr std::uint16_t pppProtocolIp = 0x0021;
constexpr std::uint16_t pppProtocolLcp = 0xc021;
constexpr std::uint16_t pppProtocolPap = 0xc023;
constexpr std::uint16_t pppProtocolIpcp = 0x8021;
constexpr std::uint16_t pppFcsGood = 0xf0b8;

std::uint16_t readBe16(const std::uint8_t* bytes)
{
    return static_cast<std::uint16_t>((static_cast<std::uint16_t>(bytes[0]) << 8) | bytes[1]);
}

void writeBe16(std::uint8_t* bytes, std::uint16_t value)
{
    bytes[0] = static_cast<std::uint8_t>(value >> 8);
    bytes[1] = static_cast<std::uint8_t>(value);
}

std::uint16_t internetChecksum(const std::uint8_t* data, qsizetype length)
{
    std::uint32_t sum = 0;
    for (qsizetype index = 0; index + 1 < length; index += 2) {
        sum += readBe16(data + index);
    }
    if ((length & 1) != 0) sum += static_cast<std::uint32_t>(data[length - 1]) << 8;
    while ((sum >> 16) != 0) sum = (sum & 0xffff) + (sum >> 16);
    return static_cast<std::uint16_t>(~sum);
}

std::uint16_t pppFcs(const std::uint8_t* data, qsizetype length)
{
    std::uint16_t fcs = 0xffff;
    for (qsizetype index = 0; index < length; ++index) {
        fcs ^= data[index];
        for (int bit = 0; bit < 8; ++bit) {
            fcs = static_cast<std::uint16_t>((fcs & 1) != 0 ? (fcs >> 1) ^ 0x8408 : fcs >> 1);
        }
    }
    return fcs;
}

QByteArray encodeSlipFrame(const std::uint8_t* payload, qsizetype length)
{
    QByteArray output;
    output.reserve(length * 2 + 2);
    output.append(static_cast<char>(slipEnd));
    for (qsizetype index = 0; index < length; ++index) {
        const auto byte = payload[index];
        if (byte == slipEnd) {
            output.append(static_cast<char>(slipEsc));
            output.append(static_cast<char>(slipEscEnd));
        } else if (byte == slipEsc) {
            output.append(static_cast<char>(slipEsc));
            output.append(static_cast<char>(slipEscEsc));
        } else {
            output.append(static_cast<char>(byte));
        }
    }
    output.append(static_cast<char>(slipEnd));
    return output;
}

QByteArray encodePppFrame(std::uint16_t protocol, const QByteArray& payload)
{
    QByteArray body;
    body.reserve(payload.size() + 6);
    body.append(static_cast<char>(0xff));
    body.append(static_cast<char>(0x03));
    body.append(static_cast<char>(protocol >> 8));
    body.append(static_cast<char>(protocol));
    body.append(payload);
    const auto fcs = static_cast<std::uint16_t>(pppFcs(reinterpret_cast<const std::uint8_t*>(body.constData()), body.size()) ^ 0xffff);
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

QString normalizedCommand(QString command)
{
    command = command.trimmed();
    if (command.startsWith(QStringLiteral("AT"), Qt::CaseInsensitive)) command.remove(0, 2);
    return command.toUpper();
}

bool parseTcpTarget(QString target, QString& host, quint16& port)
{
    if (target.startsWith(QStringLiteral("tcp:"), Qt::CaseInsensitive)) target.remove(0, 4);
    const auto colon = target.lastIndexOf(QLatin1Char(':'));
    if (colon <= 0 || colon == target.size() - 1) return false;
    bool ok = false;
    const auto parsedPort = target.mid(colon + 1).toUInt(&ok);
    if (!ok || parsedPort == 0 || parsedPort > 65535) return false;
    host = target.left(colon);
    port = static_cast<quint16>(parsedPort);
    return !host.trimmed().isEmpty();
}

} // namespace

class HayesModem::TcpConnection final : public HayesModem::Connection {
public:
    TcpConnection(HayesModem& modem, QString host, quint16 port, bool telnet)
        : m_modem(modem)
        , m_host(std::move(host))
        , m_port(port)
        , m_telnet(telnet)
        , m_thread([this]() { run(); })
    {
    }

    ~TcpConnection() override { close(); }

    void send(std::uint8_t value) override
    {
        std::lock_guard lock(m_mutex);
        m_outgoing.push_back(value);
        m_cv.notify_one();
    }

    void close() override
    {
        const bool wasStopping = m_stop.exchange(true);
        m_cv.notify_one();
        if (!wasStopping && m_thread.joinable()) m_thread.join();
        else if (m_thread.joinable()) m_thread.join();
    }

    bool connected() const override { return m_connected; }

private:
    void queueSocketBytes(const QByteArray& bytes)
    {
        if (!m_telnet) {
            m_modem.enqueueIncoming(bytes);
            return;
        }
        QByteArray clean;
        for (const auto raw : bytes) {
            const auto byte = static_cast<std::uint8_t>(raw);
            if (m_telnetIac) {
                if (byte == 0xff) clean.append(static_cast<char>(0xff));
                else if (byte >= 0xfb && byte <= 0xfe) m_telnetVerb = byte;
                m_telnetIac = false;
                continue;
            }
            if (m_telnetVerb != 0) {
                const auto responseVerb = (m_telnetVerb == 0xfd || m_telnetVerb == 0xfe) ? std::uint8_t { 0xfc } : std::uint8_t { 0xfe };
                std::lock_guard lock(m_mutex);
                m_outgoing.push_back(0xff);
                m_outgoing.push_back(responseVerb);
                m_outgoing.push_back(byte);
                m_telnetVerb = 0;
                continue;
            }
            if (byte == 0xff) {
                m_telnetIac = true;
                continue;
            }
            clean.append(static_cast<char>(byte));
        }
        if (!clean.isEmpty()) m_modem.enqueueIncoming(clean);
    }

    void run()
    {
        QTcpSocket socket;
        socket.connectToHost(m_host, m_port);
        if (!socket.waitForConnected(10000)) {
            m_connected = false;
            return;
        }
        m_connected = true;
        while (!m_stop) {
            {
                std::deque<std::uint8_t> outgoing;
                {
                    std::lock_guard lock(m_mutex);
                    outgoing.swap(m_outgoing);
                }
                if (!outgoing.empty()) {
                    QByteArray bytes;
                    bytes.reserve(static_cast<qsizetype>(outgoing.size()));
                    for (const auto value : outgoing) bytes.append(static_cast<char>(value));
                    socket.write(bytes);
                    if (!socket.waitForBytesWritten(1000)) break;
                }
            }
            if (socket.waitForReadyRead(10)) {
                const auto bytes = socket.readAll();
                if (!bytes.isEmpty()) queueSocketBytes(bytes);
            }
            if (socket.state() != QAbstractSocket::ConnectedState) break;
        }
        socket.disconnectFromHost();
        m_connected = false;
    }

    HayesModem& m_modem;
    QString m_host;
    quint16 m_port = 0;
    bool m_telnet = false;
    std::atomic_bool m_stop = false;
    std::atomic_bool m_connected = false;
    std::thread m_thread;
    mutable std::mutex m_mutex;
    std::condition_variable m_cv;
    std::deque<std::uint8_t> m_outgoing;
    bool m_telnetIac = false;
    std::uint8_t m_telnetVerb = 0;
};

class SerialIpOverEthernet {
public:
    explicit SerialIpOverEthernet(config::SerialSlipConfiguration configuration)
        : m_configuration(std::move(configuration))
    {
        m_localIp = network::parseIpv4Address(m_configuration.localIp, { 172, 16, 0, 1 });
        m_remoteIp = network::parseIpv4Address(m_configuration.remoteIp, { 172, 16, 0, 2 });
        m_localMac = { 0x52, 0x55, m_localIp[0], m_localIp[1], m_localIp[2], m_localIp[3] };
        m_remoteMac = { 0x52, 0x55, m_remoteIp[0], m_remoteIp[1], m_remoteIp[2], m_remoteIp[3] };
        network::SlirpEthernetConfiguration backendConfig;
        backendConfig.hostIp = m_configuration.localIp;
        backendConfig.guestIp = m_configuration.remoteIp;
        backendConfig.mtu = m_configuration.mtu;
        m_backend = std::make_unique<network::SlirpEthernetBackend>(std::move(backendConfig));
    }

    void poll(const std::function<void(const QByteArray&)>& receiveIpPacket)
    {
        if (!m_backend || !m_backend->connected()) return;
        m_backend->poll();
        drainBackend(receiveIpPacket);
    }

    void transmitIpPacket(const QByteArray& packet, const std::function<void(const QByteArray&)>& receiveIpPacket)
    {
        if (!m_backend || !m_backend->connected() || packet.size() < 20 || packet.size() > m_configuration.mtu) return;
        if (handleLocalIpPacket(packet, receiveIpPacket)) return;
        QByteArray frame;
        frame.resize(ethernetHeaderSize + packet.size());
        auto* out = reinterpret_cast<std::uint8_t*>(frame.data());
        std::memcpy(out, m_localMac.data(), 6);
        std::memcpy(out + 6, m_remoteMac.data(), 6);
        writeBe16(out + 12, ethertypeIpv4);
        std::memcpy(out + ethernetHeaderSize, packet.constData(), static_cast<std::size_t>(packet.size()));
        (void)m_backend->transmitFrame(frame);
        drainBackend(receiveIpPacket);
    }

    void close()
    {
        if (m_backend) m_backend->close();
    }

    [[nodiscard]] bool connected() const
    {
        return m_backend && m_backend->connected();
    }

private:
    bool handleLocalIpPacket(const QByteArray& packet, const std::function<void(const QByteArray&)>& receiveIpPacket)
    {
        const auto* ip = reinterpret_cast<const std::uint8_t*>(packet.constData());
        const auto headerLength = static_cast<qsizetype>(ip[0] & 0x0f) * 4;
        if ((ip[0] >> 4) != 4 || headerLength < 20 || packet.size() < headerLength + 8) return false;
        const auto totalLength = static_cast<qsizetype>(readBe16(ip + 2));
        if (totalLength < headerLength || totalLength > packet.size()) return false;
        if (std::memcmp(ip + 16, m_localIp.data(), 4) != 0) return false;
        if (ip[9] != 1) return true;
        if (internetChecksum(ip, headerLength) != 0) return true;
        const auto* icmp = ip + headerLength;
        const auto icmpLength = totalLength - headerLength;
        if (icmp[0] != 8 || internetChecksum(icmp, icmpLength) != 0) return true;

        QByteArray reply = packet.left(totalLength);
        auto* out = reinterpret_cast<std::uint8_t*>(reply.data());
        std::memcpy(out + 12, ip + 16, 4);
        std::memcpy(out + 16, ip + 12, 4);
        out[8] = 64;
        out[10] = 0;
        out[11] = 0;
        writeBe16(out + 10, internetChecksum(out, headerLength));
        auto* outIcmp = out + headerLength;
        outIcmp[0] = 0;
        outIcmp[2] = 0;
        outIcmp[3] = 0;
        writeBe16(outIcmp + 2, internetChecksum(outIcmp, icmpLength));
        receiveIpPacket(reply);
        return true;
    }

    void drainBackend(const std::function<void(const QByteArray&)>& receiveIpPacket)
    {
        while (m_backend) {
            auto frame = m_backend->receiveFrame();
            if (!frame) break;
            handleEthernetFrame(*frame, receiveIpPacket);
        }
    }

    void handleEthernetFrame(const QByteArray& bytes, const std::function<void(const QByteArray&)>& receiveIpPacket)
    {
        const auto* frame = reinterpret_cast<const std::uint8_t*>(bytes.constData());
        if (bytes.size() < ethernetHeaderSize) return;
        const auto ethertype = readBe16(frame + 12);
        if (ethertype == ethertypeArp) {
            handleArp(frame, static_cast<size_t>(bytes.size()));
            return;
        }
        if (ethertype != ethertypeIpv4) return;
        const auto payloadLength = static_cast<qsizetype>(bytes.size() - ethernetHeaderSize);
        if (payloadLength <= 0 || payloadLength > m_configuration.mtu) return;
        receiveIpPacket(QByteArray(reinterpret_cast<const char*>(frame + ethernetHeaderSize), payloadLength));
    }

    void handleArp(const std::uint8_t* frame, size_t len)
    {
        if (!m_backend || len < ethernetHeaderSize + arpPacketSize) return;
        if (readBe16(frame + 20) != 1) return;
        if (std::memcmp(frame + 38, m_remoteIp.data(), 4) != 0) return;
        QByteArray reply;
        reply.resize(ethernetHeaderSize + arpPacketSize);
        auto* out = reinterpret_cast<std::uint8_t*>(reply.data());
        std::memcpy(out, frame + 6, 6);
        std::memcpy(out + 6, m_remoteMac.data(), 6);
        writeBe16(out + 12, ethertypeArp);
        writeBe16(out + 14, 1);
        writeBe16(out + 16, ethertypeIpv4);
        out[18] = 6;
        out[19] = 4;
        writeBe16(out + 20, 2);
        std::memcpy(out + 22, m_remoteMac.data(), 6);
        std::memcpy(out + 28, m_remoteIp.data(), 4);
        std::memcpy(out + 32, m_localMac.data(), 6);
        std::memcpy(out + 38, m_localIp.data(), 4);
        (void)m_backend->transmitFrame(reply);
    }

    config::SerialSlipConfiguration m_configuration;
    std::unique_ptr<network::PacketNetworkBackend> m_backend;
    std::array<std::uint8_t, 4> m_localIp {};
    std::array<std::uint8_t, 4> m_remoteIp {};
    std::array<std::uint8_t, 6> m_localMac {};
    std::array<std::uint8_t, 6> m_remoteMac {};
};

class HayesModem::SlipConnection final : public HayesModem::Connection {
public:
    explicit SlipConnection(HayesModem& modem, config::SerialSlipConfiguration configuration)
        : m_modem(modem)
        , m_configuration(std::move(configuration))
        , m_network(m_configuration)
    {
    }

    ~SlipConnection() override { close(); }

    void poll() override
    {
        m_network.poll([this](const QByteArray& packet) { enqueueIpPacket(packet); });
    }

    void send(std::uint8_t value) override
    {
        if (!m_network.connected()) return;
        if (value == slipEnd) {
            if (m_droppingOversize) {
                m_frame.clear();
                m_droppingOversize = false;
                m_escapePending = false;
                return;
            }
            if (!m_frame.isEmpty()) {
                m_network.transmitIpPacket(m_frame, [this](const QByteArray& packet) { enqueueIpPacket(packet); });
                m_frame.clear();
            }
            m_escapePending = false;
            return;
        }
        if (m_droppingOversize) return;
        if (m_escapePending) {
            m_escapePending = false;
            if (value == slipEscEnd) value = slipEnd;
            else if (value == slipEscEsc) value = slipEsc;
        } else if (value == slipEsc) {
            m_escapePending = true;
            return;
        }
        if (m_frame.size() >= m_configuration.mtu) {
            m_droppingOversize = true;
            return;
        }
        m_frame.append(static_cast<char>(value));
    }

    void close() override
    {
        m_network.close();
    }

    bool connected() const override { return m_network.connected(); }

private:
    void enqueueIpPacket(const QByteArray& packet)
    {
        m_modem.enqueueIncoming(encodeSlipFrame(reinterpret_cast<const std::uint8_t*>(packet.constData()), packet.size()));
    }

    HayesModem& m_modem;
    config::SerialSlipConfiguration m_configuration;
    SerialIpOverEthernet m_network;
    QByteArray m_frame;
    bool m_escapePending = false;
    bool m_droppingOversize = false;
};

class HayesModem::PppConnection final : public HayesModem::Connection {
public:
    explicit PppConnection(HayesModem& modem, config::SerialSlipConfiguration configuration)
        : m_modem(modem)
        , m_configuration(std::move(configuration))
        , m_network(m_configuration)
    {
        m_localIp = network::parseIpv4Address(m_configuration.localIp, { 172, 16, 0, 1 });
        m_remoteIp = network::parseIpv4Address(m_configuration.remoteIp, { 172, 16, 0, 2 });
    }

    ~PppConnection() override { close(); }

    void poll() override
    {
        m_network.poll([this](const QByteArray& packet) { enqueueIpPacket(packet); });
    }

    void send(std::uint8_t value) override
    {
        if (!m_network.connected()) return;
        if (value == 0x7e) {
            if (!m_frame.isEmpty()) handlePppFrame(m_frame);
            m_frame.clear();
            m_escapePending = false;
            return;
        }
        if (m_escapePending) {
            value ^= 0x20;
            m_escapePending = false;
        } else if (value == 0x7d) {
            m_escapePending = true;
            return;
        }
        if (m_frame.size() <= m_configuration.mtu + 16) m_frame.append(static_cast<char>(value));
    }

    void close() override
    {
        m_network.close();
    }

    bool connected() const override { return m_network.connected(); }

private:
    void handlePppFrame(const QByteArray& raw)
    {
        if (raw.size() < 4) return;
        const auto* bytes = reinterpret_cast<const std::uint8_t*>(raw.constData());
        if (pppFcs(bytes, raw.size()) != pppFcsGood) return;
        qsizetype offset = 0;
        if (raw.size() >= 2 && bytes[0] == 0xff && bytes[1] == 0x03) offset = 2;
        if (offset >= raw.size() - 2) return;
        std::uint16_t protocol = 0;
        if ((bytes[offset] & 1) != 0) {
            protocol = bytes[offset++];
        } else {
            if (offset + 1 >= raw.size() - 2) return;
            protocol = readBe16(bytes + offset);
            offset += 2;
        }
        const auto payloadLength = raw.size() - offset - 2;
        const auto payload = raw.mid(offset, payloadLength);
        if (protocol == pppProtocolLcp) handleLcp(payload);
        else if (protocol == pppProtocolPap) handlePap(payload);
        else if (protocol == pppProtocolIpcp) handleIpcp(payload);
        else if (protocol == pppProtocolIp) handleIpPacket(payload);
        else sendPppControl(pppProtocolLcp, 8, nextId(), QByteArray(reinterpret_cast<const char*>(&protocol), 0));
    }

    void handleLcp(const QByteArray& packet)
    {
        if (packet.size() < 4) return;
        const auto* bytes = reinterpret_cast<const std::uint8_t*>(packet.constData());
        const auto code = bytes[0];
        const auto id = bytes[1];
        const auto length = std::min<int>(readBe16(bytes + 2), packet.size());
        const auto data = packet.mid(4, length - 4);
        if (code == 1) sendPppControl(pppProtocolLcp, 2, id, data);
        else if (code == 5) sendPppControl(pppProtocolLcp, 6, id, data);
        else if (code == 9) sendPppControl(pppProtocolLcp, 10, id, data);
    }

    void handlePap(const QByteArray& packet)
    {
        if (packet.size() < 4) return;
        const auto* bytes = reinterpret_cast<const std::uint8_t*>(packet.constData());
        if (bytes[0] != 1) return;
        QByteArray message;
        message.append(static_cast<char>(2));
        message.append(static_cast<char>('O'));
        message.append(static_cast<char>('K'));
        sendPppControl(pppProtocolPap, 2, bytes[1], message);
    }

    void handleIpcp(const QByteArray& packet)
    {
        if (packet.size() < 4) return;
        const auto* bytes = reinterpret_cast<const std::uint8_t*>(packet.constData());
        const auto code = bytes[0];
        const auto id = bytes[1];
        const auto length = std::min<int>(readBe16(bytes + 2), packet.size());
        const auto options = packet.mid(4, length - 4);
        if (code != 1) return;

        QByteArray nak;
        bool needsNak = false;
        for (qsizetype offset = 0; offset + 2 <= options.size();) {
            const auto type = static_cast<std::uint8_t>(options[offset]);
            const auto optionLength = static_cast<std::uint8_t>(options[offset + 1]);
            if (optionLength < 2 || offset + optionLength > options.size()) break;
            const auto* option = reinterpret_cast<const std::uint8_t*>(options.constData() + offset);
            if ((type == 3 || type == 129 || type == 131) && optionLength == 6) {
                const auto wanted = type == 3 ? m_remoteIp : m_localIp;
                if (std::memcmp(option + 2, wanted.data(), 4) != 0) {
                    needsNak = true;
                    nak.append(static_cast<char>(type));
                    nak.append(static_cast<char>(6));
                    nak.append(reinterpret_cast<const char*>(wanted.data()), 4);
                }
            }
            offset += optionLength;
        }
        if (needsNak) sendPppControl(pppProtocolIpcp, 3, id, nak);
        else {
            sendPppControl(pppProtocolIpcp, 2, id, options);
            if (!m_sentIpcpRequest) {
                QByteArray requestOptions;
                requestOptions.append(static_cast<char>(3));
                requestOptions.append(static_cast<char>(6));
                requestOptions.append(reinterpret_cast<const char*>(m_localIp.data()), 4);
                sendPppControl(pppProtocolIpcp, 1, nextId(), requestOptions);
                m_sentIpcpRequest = true;
            }
        }
    }

    void sendPppControl(std::uint16_t protocol, std::uint8_t code, std::uint8_t id, const QByteArray& data)
    {
        QByteArray payload;
        payload.append(static_cast<char>(code));
        payload.append(static_cast<char>(id));
        const auto length = static_cast<std::uint16_t>(data.size() + 4);
        payload.append(static_cast<char>(length >> 8));
        payload.append(static_cast<char>(length));
        payload.append(data);
        m_modem.enqueueIncoming(encodePppFrame(protocol, payload));
    }

    std::uint8_t nextId() { return ++m_nextId; }

    void handleIpPacket(const QByteArray& packet)
    {
        m_network.transmitIpPacket(packet, [this](const QByteArray& response) { enqueueIpPacket(response); });
    }

    void enqueueIpPacket(const QByteArray& packet)
    {
        m_modem.enqueueIncoming(encodePppFrame(pppProtocolIp, packet));
    }

    HayesModem& m_modem;
    config::SerialSlipConfiguration m_configuration;
    SerialIpOverEthernet m_network;
    std::array<std::uint8_t, 4> m_localIp {};
    std::array<std::uint8_t, 4> m_remoteIp {};
    QByteArray m_frame;
    bool m_escapePending = false;
    bool m_sentIpcpRequest = false;
    std::uint8_t m_nextId = 0x40;
};

HayesModem::HayesModem(config::SerialDeviceConfiguration configuration)
    : m_configuration(std::move(configuration))
{
    if (m_configuration.phonebook.isEmpty()) m_configuration.phonebook = config::defaultSerialModemPhonebook();
}

HayesModem::~HayesModem()
{
    hangup();
}

void HayesModem::reset()
{
    hangup();
    m_mode = Mode::Command;
    m_echo = true;
    m_quiet = false;
    m_commandLine.clear();
    m_escapePlusCount = 0;
    std::lock_guard lock(m_incomingMutex);
    m_incoming.clear();
}

void HayesModem::poll()
{
    if (m_connection) m_connection->poll();
    for (int count = 0; count < maxPollBytes; ++count) {
        std::uint8_t value = 0;
        {
            std::lock_guard lock(m_incomingMutex);
            if (m_incoming.empty()) return;
            value = m_incoming.front();
            m_incoming.pop_front();
        }
        transmitByte(value);
    }
}

void HayesModem::receiveByte(std::uint8_t value)
{
    if (m_mode == Mode::Online) receiveOnlineByte(value);
    else receiveCommandByte(value);
}

void HayesModem::receiveCommandByte(std::uint8_t value)
{
    if (m_echo) enqueueIncoming(value);
    if (value == '\r') {
        const auto line = m_commandLine;
        m_commandLine.clear();
        handleCommand(line);
        return;
    }
    if (value == '\n') return;
    if (value == 0x08 || value == 0x7f) {
        if (!m_commandLine.isEmpty()) m_commandLine.chop(1);
        return;
    }
    m_commandLine.append(QChar(static_cast<ushort>(value)));
}

void HayesModem::receiveOnlineByte(std::uint8_t value)
{
    if (value == '+') {
        ++m_escapePlusCount;
        if (m_escapePlusCount == 3) {
            m_mode = Mode::Command;
            m_escapePlusCount = 0;
            result(QStringLiteral("OK"));
        }
        return;
    }
    if (m_escapePlusCount > 0 && m_connection && m_connection->connected()) {
        for (int i = 0; i < m_escapePlusCount; ++i) m_connection->send('+');
    }
    m_escapePlusCount = 0;
    if (m_connection && m_connection->connected()) m_connection->send(value);
    else {
        m_mode = Mode::Command;
        result(QStringLiteral("NO CARRIER"));
    }
}

void HayesModem::handleCommand(QString command)
{
    command = normalizedCommand(std::move(command));
    if (command.isEmpty()) {
        result(QStringLiteral("OK"));
        return;
    }
    if (command == QStringLiteral("Z")) {
        reset();
        result(QStringLiteral("OK"));
        return;
    }
    if (command == QStringLiteral("E0")) {
        m_echo = false;
        result(QStringLiteral("OK"));
        return;
    }
    if (command == QStringLiteral("E1")) {
        m_echo = true;
        result(QStringLiteral("OK"));
        return;
    }
    if (command == QStringLiteral("Q0")) {
        m_quiet = false;
        result(QStringLiteral("OK"));
        return;
    }
    if (command == QStringLiteral("Q1")) {
        m_quiet = true;
        return;
    }
    if (command == QStringLiteral("H") || command == QStringLiteral("H0")) {
        hangup();
        result(QStringLiteral("OK"));
        return;
    }
    if (command == QStringLiteral("O") || command == QStringLiteral("O0")) {
        if (m_connection && m_connection->connected()) {
            m_mode = Mode::Online;
            result(QStringLiteral("CONNECT"));
        } else {
            result(QStringLiteral("NO CARRIER"));
        }
        return;
    }
    if (command.startsWith(QStringLiteral("D"))) {
        auto target = command.mid(1);
        if (target.startsWith(QStringLiteral("T")) || target.startsWith(QStringLiteral("P"))) target.remove(0, 1);
        dial(target);
        return;
    }
    result(QStringLiteral("ERROR"));
}

void HayesModem::dial(const QString& dialString)
{
    const auto target = resolveDialTarget(dialString);
    if (!target) {
        result(QStringLiteral("NO CARRIER"));
        return;
    }
    if (target->target.startsWith(QStringLiteral("slip:"), Qt::CaseInsensitive)) {
        connectSlip();
        return;
    }
    if (target->target.startsWith(QStringLiteral("ppp:"), Qt::CaseInsensitive)) {
        connectPpp();
        return;
    }
    if (target->target.startsWith(QStringLiteral("tcp:"), Qt::CaseInsensitive)) {
        connectTcp(target->target, target->telnet);
        return;
    }
    result(QStringLiteral("NO CARRIER"));
}

std::optional<config::SerialPhonebookEntry> HayesModem::resolveDialTarget(const QString& dialString) const
{
    const auto trimmed = dialString.trimmed();
    for (const auto& entry : m_configuration.phonebook) {
        if (entry.number.compare(trimmed, Qt::CaseInsensitive) == 0) return entry;
    }
    if (m_configuration.directTcpDialing) {
        QString host;
        quint16 port = 0;
        if (parseTcpTarget(trimmed, host, port)) return config::SerialPhonebookEntry { trimmed, QStringLiteral("tcp:") + trimmed, false };
    }
    return std::nullopt;
}

void HayesModem::connectTcp(const QString& target, bool telnet)
{
    QString host;
    quint16 port = 0;
    if (!parseTcpTarget(target, host, port)) {
        result(QStringLiteral("NO CARRIER"));
        return;
    }
    hangup();
    auto connection = std::make_unique<TcpConnection>(*this, host, port, telnet);
    for (int tries = 0; tries < 100 && !connection->connected(); ++tries) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    if (!connection->connected()) {
        result(QStringLiteral("NO CARRIER"));
        return;
    }
    m_connection = std::move(connection);
    m_mode = Mode::Online;
    result(QStringLiteral("CONNECT"));
}

void HayesModem::connectSlip()
{
    if (!m_configuration.slip.enabled) {
        result(QStringLiteral("NO CARRIER"));
        return;
    }
    hangup();
    auto connection = std::make_unique<SlipConnection>(*this, m_configuration.slip);
    if (!connection->connected()) {
        result(QStringLiteral("NO CARRIER"));
        return;
    }
    m_connection = std::move(connection);
    m_mode = Mode::Online;
    result(QStringLiteral("CONNECT"));
}

void HayesModem::connectPpp()
{
    if (!m_configuration.slip.enabled) {
        result(QStringLiteral("NO CARRIER"));
        return;
    }
    hangup();
    auto connection = std::make_unique<PppConnection>(*this, m_configuration.slip);
    if (!connection->connected()) {
        result(QStringLiteral("NO CARRIER"));
        return;
    }
    m_connection = std::move(connection);
    m_mode = Mode::Online;
    result(QStringLiteral("CONNECT"));
}

void HayesModem::hangup()
{
    if (m_connection) {
        m_connection->close();
        m_connection.reset();
    }
    m_mode = Mode::Command;
}

void HayesModem::enqueueText(const QString& text)
{
    const auto bytes = text.toLatin1();
    enqueueIncoming(bytes);
}

void HayesModem::enqueueIncoming(const QByteArray& bytes)
{
    std::lock_guard lock(m_incomingMutex);
    for (const auto value : bytes) m_incoming.push_back(static_cast<std::uint8_t>(value));
}

void HayesModem::enqueueIncoming(std::uint8_t value)
{
    std::lock_guard lock(m_incomingMutex);
    m_incoming.push_back(value);
}

void HayesModem::result(const QString& text)
{
    if (!m_quiet) enqueueText(QStringLiteral("\r\n") + text + QStringLiteral("\r\n"));
}

} // namespace cutemac::devices::modem
