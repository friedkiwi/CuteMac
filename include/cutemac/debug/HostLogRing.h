#pragma once

#include <QStringList>

namespace cutemac::debug {

// Bounded ring of host-side Qt log messages. Without it qWarning output scrolls
// off the terminal and is gone by the time a panic dump is taken, which is
// exactly when it matters. Installed once at startup in debug builds.
class HostLogRing {
public:
    static constexpr int capacity = 512;

    // Chains to any previously installed handler so normal terminal output is
    // unaffected.
    static void install();
    [[nodiscard]] static QStringList entries();
    static void clear();
};

} // namespace cutemac::debug
