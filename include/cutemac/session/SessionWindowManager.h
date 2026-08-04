#pragma once

#include <QHash>
#include <QObject>
#include <QPointer>
#include <QString>

#include <functional>

#include "cutemac/config/Configuration.h"

class QWidget;

namespace cutemac::session {

class EmulatorWindow;

// Owns every live emulator window in the process. Sessions are keyed by the
// canonical path of their backing profile so one profile can never be driven by
// two windows writing the same file.
class SessionWindowManager final : public QObject {
public:
    explicit SessionWindowManager(QObject* parent = nullptr);

    // Starts a session for the profile, or raises the existing window when that
    // profile is already running. Returns false only when no session could be
    // started, in which case `error` receives a user-facing explanation.
    bool open(const config::Configuration& configuration, const QString& profilePath, QString* error = nullptr);

    // The window a session's "Session Manager" action raises. Sessions started
    // before this is set do not get the action, so set it before opening any.
    void setProfileManagerWindow(QWidget* window);

    [[nodiscard]] bool isOpen(const QString& profilePath) const;
    [[nodiscard]] int openSessionCount() const;

    // Invoked whenever a session opens or closes. This project builds without
    // moc, so observers use a plain callback rather than a Qt signal.
    void setSessionsChangedCallback(std::function<void()> callback);

private:
    [[nodiscard]] static QString sessionKey(const QString& profilePath);
    void forget(const QString& key);
    void notifySessionsChanged();

    QHash<QString, QPointer<EmulatorWindow>> m_sessions;
    QPointer<QWidget> m_profileManagerWindow;
    std::function<void()> m_sessionsChanged;
    quint64 m_unsavedSessionSerial = 0;
};

} // namespace cutemac::session
