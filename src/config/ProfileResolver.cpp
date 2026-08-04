#include "cutemac/config/ProfileResolver.h"

#include <QFileInfo>
#include <QStringList>
#include <QVector>

namespace cutemac::config {

namespace {

bool matches(const QString& candidate, const QString& request)
{
    return candidate.compare(request, Qt::CaseInsensitive) == 0;
}

// Profile paths become session identity, so report the file as it exists on
// disk. Case-insensitive filesystems otherwise hand back whatever casing the
// caller typed, which would key two spellings of one profile separately.
QString onDiskPath(const QString& path)
{
    const QFileInfo info(path);
    const auto canonical = info.canonicalFilePath();
    return canonical.isEmpty() ? info.absoluteFilePath() : canonical;
}

} // namespace

ProfileResolution resolveProfile(const QString& request, ResolvedProfile& resolved, QString* detail)
{
    const auto setDetail = [detail](const QString& text) {
        if (detail != nullptr) *detail = text;
    };
    setDetail(QString());

    const auto trimmed = request.trimmed();
    if (trimmed.isEmpty()) {
        setDetail(ConfigurationManager::profileDirectoryPath());
        return ProfileResolution::NotFound;
    }

    const ConfigurationManager manager;

    const QFileInfo requestedFile(trimmed);
    if (requestedFile.isFile()) {
        const auto path = onDiskPath(trimmed);
        const auto configuration = manager.loadTomlFile(path);
        if (!configuration.has_value()) {
            setDetail(path);
            return ProfileResolution::Unreadable;
        }
        resolved = { path, *configuration };
        return ProfileResolution::Ok;
    }

    // A bare name refers to a profile in the managed directory. Scanning the
    // directory rather than probing profilePathForName() keeps the reported path
    // in its real on-disk spelling: a case-insensitive filesystem would happily
    // answer an exists() probe for a path that is cased differently on disk.
    QVector<ResolvedProfile> exactName;
    QVector<ResolvedProfile> looseName;
    QVector<ResolvedProfile> fileBase;
    for (const auto& path : manager.profileFilePaths()) {
        const auto configuration = manager.loadTomlFile(path);
        if (!configuration.has_value()) {
            continue;
        }
        const ResolvedProfile candidate { onDiskPath(path), *configuration };
        if (configuration->profileName == trimmed) {
            exactName.append(candidate);
        } else if (matches(configuration->profileName, trimmed)) {
            looseName.append(candidate);
        } else if (matches(QFileInfo(path).completeBaseName(), trimmed)) {
            // The slugged file name the manager writes, e.g. "System_7_Build".
            fileBase.append(candidate);
        }
    }

    // An exact spelling outranks a case-insensitive one, and a stored profile
    // name outranks a bare file name, so renaming or re-casing a profile never
    // leaves an older spelling shadowing it.
    const auto& candidates = !exactName.isEmpty() ? exactName
        : (!looseName.isEmpty() ? looseName : fileBase);
    if (candidates.isEmpty()) {
        setDetail(ConfigurationManager::profileDirectoryPath());
        return ProfileResolution::NotFound;
    }
    if (candidates.size() > 1) {
        QStringList paths;
        paths.reserve(static_cast<int>(candidates.size()));
        for (const auto& candidate : candidates) {
            paths.append(candidate.path);
        }
        setDetail(paths.join(QLatin1Char('\n')));
        return ProfileResolution::Ambiguous;
    }

    resolved = candidates.first();
    return ProfileResolution::Ok;
}

QString profileResolutionMessage(ProfileResolution resolution, const QString& request, const QString& detail)
{
    switch (resolution) {
    case ProfileResolution::Ok:
        return {};
    case ProfileResolution::NotFound:
        return QStringLiteral("No profile named \"%1\" was found.\n\nProfiles are stored in:\n%2").arg(request, detail);
    case ProfileResolution::Ambiguous:
        return QStringLiteral("\"%1\" matches more than one profile:\n\n%2").arg(request, detail);
    case ProfileResolution::Unreadable:
        return QStringLiteral("Could not read the profile:\n%1").arg(detail);
    }
    return {};
}

} // namespace cutemac::config
