#pragma once

#include <QString>

class QKeyEvent;

namespace cutemac::session {

class HostInputMapper {
public:
    [[nodiscard]] static bool supportsRelativeCapture(const QString& platformName);
    [[nodiscard]] static QString releaseChordLabel();
    [[nodiscard]] static bool isReleaseChord(const QKeyEvent& event);
    [[nodiscard]] static int macKeyCode(const QKeyEvent& event);
};

} // namespace cutemac::session
