#pragma once

#include <QObject>
#include <QTcpServer>

class QTcpSocket;

namespace cutemac::core {
class EmulationSession;
class SessionRunner;
}

namespace cutemac::session {

class SessionControlServer final : public QObject {
public:
    SessionControlServer(core::EmulationSession& session, core::SessionRunner& runner, QObject* parent = nullptr);

    [[nodiscard]] bool listen(quint16 port = 0);
    [[nodiscard]] quint16 port() const;

private:
    void acceptConnection();
    void processLine(QTcpSocket& socket, const QByteArray& line);
    void sendStatus(QTcpSocket& socket);

    core::EmulationSession& m_session;
    core::SessionRunner& m_runner;
    QTcpServer m_server;
};

} // namespace cutemac::session
