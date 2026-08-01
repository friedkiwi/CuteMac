#include "cutemac/session/SessionControlServer.h"

#include <QHostAddress>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTcpSocket>

#include "cutemac/core/EmulationSession.h"
#include "cutemac/core/SessionRunner.h"

namespace cutemac::session {

SessionControlServer::SessionControlServer(core::EmulationSession& session, core::SessionRunner& runner, QObject* parent)
    : QObject(parent)
    , m_session(session)
    , m_runner(runner)
{
    connect(&m_server, &QTcpServer::newConnection, this, [this]() { acceptConnection(); });
}

bool SessionControlServer::listen(quint16 port)
{
#if defined(Q_OS_WASM)
    Q_UNUSED(port);
    return false;
#else
    return m_server.listen(QHostAddress::LocalHost, port);
#endif
}

quint16 SessionControlServer::port() const { return m_server.serverPort(); }

void SessionControlServer::acceptConnection()
{
    while (auto* socket = m_server.nextPendingConnection()) {
        socket->setParent(this);
        connect(socket, &QTcpSocket::readyRead, this, [this, socket]() {
            while (socket->canReadLine()) {
                processLine(*socket, socket->readLine().trimmed());
            }
        });
        connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
    }
}

void SessionControlServer::processLine(QTcpSocket& socket, const QByteArray& line)
{
    const auto document = QJsonDocument::fromJson(line);
    const auto command = document.object().value(QStringLiteral("command")).toString();
    if (command == QStringLiteral("pause")) {
        m_runner.setPaused(true);
    } else if (command == QStringLiteral("resume")) {
        m_runner.setPaused(false);
    } else if (command == QStringLiteral("reset")) {
        m_session.reset();
    } else if (command == QStringLiteral("set_speed")) {
        const auto speed = document.object().value(QStringLiteral("speed")).toString();
        if (speed != QStringLiteral("realtime") && speed != QStringLiteral("unlimited")) {
            socket.write(QJsonDocument(QJsonObject { { QStringLiteral("ok"), false }, { QStringLiteral("error"), QStringLiteral("speed must be realtime or unlimited") } }).toJson(QJsonDocument::Compact) + '\n');
            return;
        }
        m_runner.setSpeed(config::runtimeSpeedFromName(speed));
    } else if (command == QStringLiteral("mouse_position")) {
        m_session.queueMousePosition(static_cast<std::int16_t>(document.object().value(QStringLiteral("x")).toInt()),
            static_cast<std::int16_t>(document.object().value(QStringLiteral("y")).toInt()));
    } else if (command == QStringLiteral("mouse_button")) {
        m_session.queueMouseButton(document.object().value(QStringLiteral("pressed")).toBool());
    } else if (command == QStringLiteral("key")) {
        m_session.queueKey(static_cast<std::uint8_t>(document.object().value(QStringLiteral("code")).toInt()),
            document.object().value(QStringLiteral("pressed")).toBool());
    } else if (command != QStringLiteral("status")) {
        socket.write(QJsonDocument(QJsonObject { { QStringLiteral("ok"), false }, { QStringLiteral("error"), QStringLiteral("unknown command") } }).toJson(QJsonDocument::Compact) + '\n');
        return;
    }
    sendStatus(socket);
}

void SessionControlServer::sendStatus(QTcpSocket& socket)
{
    const auto status = m_session.status();
    const QJsonObject response {
        { QStringLiteral("ok"), true },
        { QStringLiteral("machine"), status.machineId },
        { QStringLiteral("pc"), static_cast<qint64>(status.programCounter) },
        { QStringLiteral("cycles"), static_cast<qint64>(status.cycles) },
        { QStringLiteral("paused"), status.paused },
        { QStringLiteral("speed"), config::runtimeSpeedName(m_runner.speed()) },
        { QStringLiteral("rom_loaded"), status.romLoaded },
    };
    socket.write(QJsonDocument(response).toJson(QJsonDocument::Compact) + '\n');
}

} // namespace cutemac::session
