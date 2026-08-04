#include "cutemac/session/SessionWindowManager.h"

#include <QFileInfo>

#include "cutemac/session/EmulatorWindow.h"

namespace cutemac::session {

SessionWindowManager::SessionWindowManager(QObject* parent)
    : QObject(parent)
{
}

QString SessionWindowManager::sessionKey(const QString& profilePath)
{
    const QFileInfo info(profilePath);
    // The profile may not exist on disk yet, in which case canonicalFilePath()
    // is empty and the absolute path is the best identity available.
    const auto canonical = info.canonicalFilePath();
    return canonical.isEmpty() ? info.absoluteFilePath() : canonical;
}

bool SessionWindowManager::open(const config::Configuration& configuration, const QString& profilePath, QString* error)
{
    if (!profilePath.isEmpty()) {
        const auto key = sessionKey(profilePath);
        const auto existing = m_sessions.constFind(key);
        if (existing != m_sessions.constEnd() && !existing.value().isNull()) {
            auto* window = existing.value().data();
            window->show();
            window->raise();
            window->activateWindow();
            return true;
        }
    }

#if defined(Q_OS_WASM)
    // The wasm runner drives every session from the single browser thread and
    // shares one audio context, so concurrent sessions would starve each other.
    if (openSessionCount() > 0) {
        if (error != nullptr) {
            *error = QStringLiteral("Close the running session before starting another one.");
        }
        return false;
    }
#else
    Q_UNUSED(error);
#endif

    // Sessions without a backing profile file cannot be deduplicated, so give
    // each one an identity that never collides.
    const auto key = profilePath.isEmpty()
        ? QStringLiteral("<unsaved-%1>").arg(++m_unsavedSessionSerial)
        : sessionKey(profilePath);

    auto* window = new EmulatorWindow(configuration, profilePath, m_profileManagerWindow.data());
    window->setAttribute(Qt::WA_DeleteOnClose);
    m_sessions.insert(key, window);
    QObject::connect(window, &QObject::destroyed, this, [this, key]() { forget(key); });
    window->show();
    // Observers reload their own state here, so nothing may touch the caller's
    // arguments after this point: they can be owned by what the callback rebuilds.
    notifySessionsChanged();
    return true;
}

void SessionWindowManager::setProfileManagerWindow(QWidget* window)
{
    m_profileManagerWindow = window;
}

bool SessionWindowManager::isOpen(const QString& profilePath) const
{
    if (profilePath.isEmpty()) return false;
    const auto existing = m_sessions.constFind(sessionKey(profilePath));
    return existing != m_sessions.constEnd() && !existing.value().isNull();
}

int SessionWindowManager::openSessionCount() const
{
    int count = 0;
    for (const auto& window : m_sessions) {
        if (!window.isNull()) ++count;
    }
    return count;
}

void SessionWindowManager::setSessionsChangedCallback(std::function<void()> callback)
{
    m_sessionsChanged = std::move(callback);
}

void SessionWindowManager::forget(const QString& key)
{
    m_sessions.remove(key);
    notifySessionsChanged();
}

void SessionWindowManager::notifySessionsChanged()
{
    if (m_sessionsChanged) m_sessionsChanged();
}

} // namespace cutemac::session
