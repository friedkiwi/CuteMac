#include "cutemac/devices/modem/NullModem.h"

#include <QHostAddress>
#include <QTcpServer>
#include <QTcpSocket>

namespace cutemac::devices::modem {

namespace {

constexpr int maxPollBytes = 256;

QHostAddress hostAddressOrAny(const QString& text)
{
    if (text.trimmed().isEmpty()) return QHostAddress::Any;
    QHostAddress address;
    return address.setAddress(text.trimmed()) ? address : QHostAddress::Any;
}

} // namespace

NullModem::NullModem(config::SerialDeviceConfiguration configuration)
    : m_configuration(std::move(configuration))
    , m_thread([this]() { run(); })
{
}

NullModem::~NullModem()
{
    m_stop = true;
    m_outgoingCv.notify_one();
    if (m_thread.joinable()) m_thread.join();
}

void NullModem::reset()
{
    {
        std::lock_guard lock(m_outgoingMutex);
        m_outgoing.clear();
    }
    {
        std::lock_guard lock(m_incomingMutex);
        m_incoming.clear();
    }
}

void NullModem::poll()
{
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

void NullModem::receiveByte(std::uint8_t value)
{
    {
        std::lock_guard lock(m_outgoingMutex);
        m_outgoing.push_back(value);
    }
    m_outgoingCv.notify_one();
}

void NullModem::run()
{
    std::unique_ptr<QTcpServer> server;
    std::unique_ptr<QTcpSocket> ownedSocket;
    QTcpSocket* socket = nullptr;

    if (m_configuration.tcpMode == config::SerialTcpMode::Listen) {
        server = std::make_unique<QTcpServer>();
        if (!server->listen(hostAddressOrAny(m_configuration.tcpHost), static_cast<quint16>(m_configuration.tcpPort))) return;
    } else {
        ownedSocket = std::make_unique<QTcpSocket>();
        ownedSocket->connectToHost(m_configuration.tcpHost, static_cast<quint16>(m_configuration.tcpPort));
        if (!ownedSocket->waitForConnected(10000)) return;
        socket = ownedSocket.get();
    }

    while (!m_stop) {
        if (server && socket == nullptr) {
            if (server->waitForNewConnection(10)) {
                ownedSocket.reset(server->nextPendingConnection());
                socket = ownedSocket.get();
            }
            continue;
        }
        if (socket == nullptr) break;

        std::deque<std::uint8_t> outgoing;
        {
            std::lock_guard lock(m_outgoingMutex);
            outgoing.swap(m_outgoing);
        }
        if (!outgoing.empty()) {
            QByteArray bytes;
            bytes.reserve(static_cast<qsizetype>(outgoing.size()));
            for (const auto value : outgoing) bytes.append(static_cast<char>(value));
            socket->write(bytes);
            if (!socket->waitForBytesWritten(1000)) break;
        }

        if (socket->waitForReadyRead(10)) {
            const auto bytes = socket->readAll();
            if (!bytes.isEmpty()) enqueueIncoming(bytes);
        }
        if (socket->state() != QAbstractSocket::ConnectedState) {
            ownedSocket.reset();
            socket = nullptr;
            if (!server) break;
        }
    }
}

void NullModem::enqueueIncoming(const QByteArray& bytes)
{
    std::lock_guard lock(m_incomingMutex);
    for (const auto value : bytes) m_incoming.push_back(static_cast<std::uint8_t>(value));
}

} // namespace cutemac::devices::modem
