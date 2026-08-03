#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>

#include <QString>
#include <QVector>

#include "cutemac/config/Configuration.h"
#include "cutemac/devices/serial/SerialEndpoint.h"

namespace cutemac::devices::modem {

class HayesModem final : public serial::SerialEndpoint {
public:
    explicit HayesModem(config::SerialDeviceConfiguration configuration);
    ~HayesModem() override;

    void reset() override;
    void poll() override;
    void receiveByte(std::uint8_t value) override;

private:
    class Connection {
    public:
        virtual ~Connection() = default;
        virtual void poll() {}
        virtual void send(std::uint8_t value) = 0;
        virtual void close() = 0;
        [[nodiscard]] virtual bool connected() const = 0;
    };

    class TcpConnection;
    class SlipConnection;
    class PppConnection;

    enum class Mode {
        Command,
        Online,
    };

    void receiveCommandByte(std::uint8_t value);
    void receiveOnlineByte(std::uint8_t value);
    void handleCommand(QString command);
    void dial(const QString& dialString);
    [[nodiscard]] std::optional<config::SerialPhonebookEntry> resolveDialTarget(const QString& dialString) const;
    void connectTcp(const QString& target, bool telnet);
    void connectSlip();
    void connectPpp();
    void hangup();
    void enqueueText(const QString& text);
    void enqueueIncoming(const QByteArray& bytes);
    void enqueueIncoming(std::uint8_t value);
    void result(const QString& text);

    config::SerialDeviceConfiguration m_configuration;
    Mode m_mode = Mode::Command;
    bool m_echo = true;
    bool m_quiet = false;
    QString m_commandLine;
    int m_escapePlusCount = 0;
    std::unique_ptr<Connection> m_connection;
    std::mutex m_incomingMutex;
    std::deque<std::uint8_t> m_incoming;
};

} // namespace cutemac::devices::modem
