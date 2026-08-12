#pragma once

#include <QString>

class QKeyEvent;

namespace cutemac::session {

class HostInputMapper {
public:
    // Whether the portable recenter-the-cursor fallback can work on this
    // platform. A native HostRelativeMouseCapture backend is independent of
    // this and is always tried first.
    [[nodiscard]] static bool supportsPointerWarpCapture(const QString& platformName);
    [[nodiscard]] static QString releaseChordLabel();
    [[nodiscard]] static bool isReleaseChord(const QKeyEvent& event);
    [[nodiscard]] static int macKeyCode(const QKeyEvent& event);
};

} // namespace cutemac::session
