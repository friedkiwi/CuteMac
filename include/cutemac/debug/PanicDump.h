#pragma once

#include <chrono>
#include <optional>

#include <QByteArray>
#include <QDateTime>
#include <QString>
#include <QStringList>

#include "cutemac/config/Configuration.h"
#include "cutemac/debug/MachineSnapshot.h"

namespace cutemac::core {
class IDebugMachineAccess;
}

namespace cutemac::debug {

inline constexpr std::chrono::milliseconds defaultPanicLockTimeout { 750 };

struct PanicDumpRequest {
    // Both configurations are carried: the profile as it was loaded and the
    // live one, because inserted media and speed drift apart during a session.
    config::Configuration startupConfiguration;
    config::Configuration runtimeConfiguration;
    QString profilePath;
    QString note;             // operator's free-text answer to "what were you doing?"
    QStringList hostLog;      // captured qWarning/qInfo ring
    QString directory;        // empty selects the default debug_dumps directory
    // Pre-rendered screenshot. Framebuffer conversion belongs to the session
    // frontend adapters, so the caller that owns FramebufferRenderer supplies
    // the encoded PNG rather than the core reaching up into the frontend.
    QByteArray screenshotPng;
};

struct PanicDumpResult {
    bool ok = false;
    QString path;
    QStringList warnings;
    bool degraded = false;
    qint64 sizeBytes = 0;
    int memberCount = 0;
};

// debug_dumps under the application data location. An .app bundle directory is
// not reliably writable, so "next to the binary" is not portable.
// CUTEMAC_PANIC_DUMP_DIR overrides it.
[[nodiscard]] QString panicDumpDirectory();

[[nodiscard]] QString panicDumpFileName(const QString& machineId, const QDateTime& timestamp);

// Serializes an already-captured snapshot. Split from capture so tests and the
// debug session can round-trip without a live machine.
[[nodiscard]] PanicDumpResult writePanicDump(const MachineSnapshot& snapshot, const PanicDumpRequest& request);

// Captures through the debug boundary and writes the archive. Prints the
// trigger and resulting path to stdout: the announcement is the point of the
// button when the session is otherwise unresponsive.
[[nodiscard]] PanicDumpResult capturePanicDump(core::IDebugMachineAccess& access,
    const PanicDumpRequest& request, std::chrono::milliseconds lockTimeout = defaultPanicLockTimeout);

[[nodiscard]] QByteArray serializeSnapshot(const MachineSnapshot& snapshot);
[[nodiscard]] std::optional<MachineSnapshot> deserializeSnapshot(const QByteArray& json);

// Reads a written archive back into a snapshot, restoring the memory region
// contents from their archive members.
[[nodiscard]] std::optional<MachineSnapshot> loadPanicDump(const QString& path, QString& error);

} // namespace cutemac::debug
