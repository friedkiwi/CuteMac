#include "cutemac/debug/HostLogRing.h"

#include <cstdio>
#include <mutex>

#include <QDateTime>
#include <QtGlobal>

namespace cutemac::debug {

namespace {

std::mutex g_mutex;
QStringList g_entries;
QtMessageHandler g_previousHandler = nullptr;
bool g_installed = false;

const char* levelName(QtMsgType type)
{
    switch (type) {
    case QtDebugMsg: return "debug";
    case QtInfoMsg: return "info";
    case QtWarningMsg: return "warning";
    case QtCriticalMsg: return "critical";
    case QtFatalMsg: return "fatal";
    }
    return "unknown";
}

void handler(QtMsgType type, const QMessageLogContext& context, const QString& message)
{
    {
        std::lock_guard lock(g_mutex);
        g_entries.append(QStringLiteral("%1 %2 %3")
                             .arg(QDateTime::currentDateTime().toString(Qt::ISODateWithMs),
                                 QString::fromLatin1(levelName(type)), message));
        while (g_entries.size() > HostLogRing::capacity) g_entries.removeFirst();
    }
    if (g_previousHandler != nullptr) {
        g_previousHandler(type, context, message);
        return;
    }
    // No prior handler means Qt's default was in place. Mirror it to stderr so
    // installing the ring never silences normal terminal output.
    std::fprintf(stderr, "%s: %s\n", levelName(type), message.toLocal8Bit().constData());
}

} // namespace

void HostLogRing::install()
{
    std::lock_guard lock(g_mutex);
    if (g_installed) return;
    g_previousHandler = qInstallMessageHandler(handler);
    g_installed = true;
}

QStringList HostLogRing::entries()
{
    std::lock_guard lock(g_mutex);
    return g_entries;
}

void HostLogRing::clear()
{
    std::lock_guard lock(g_mutex);
    g_entries.clear();
}

} // namespace cutemac::debug
