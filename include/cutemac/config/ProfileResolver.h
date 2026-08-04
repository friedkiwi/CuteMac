#pragma once

#include <QString>

#include "cutemac/config/Configuration.h"

namespace cutemac::config {

enum class ProfileResolution {
    Ok,
    NotFound,
    Ambiguous,
    Unreadable,
};

struct ResolvedProfile {
    QString path;
    Configuration configuration;
};

// Resolves a command-line profile request, which is either a path to a TOML
// profile or the name of a profile stored in the managed profile directory.
// On failure `detail` receives the profile directory, the offending path, or
// the ambiguous candidate list, depending on the resolution result.
[[nodiscard]] ProfileResolution resolveProfile(const QString& request, ResolvedProfile& resolved, QString* detail = nullptr);

[[nodiscard]] QString profileResolutionMessage(ProfileResolution resolution, const QString& request, const QString& detail);

} // namespace cutemac::config
