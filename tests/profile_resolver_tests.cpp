#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
#include <QTemporaryDir>

#include <iostream>

#include "cutemac/config/ProfileResolver.h"

namespace {

bool expect(bool condition, const char* message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
    }
    return condition;
}

// resolveProfile() reports paths as they exist on disk, so expectations are
// compared in the same form.
QString onDisk(const QString& path)
{
    const QFileInfo info(path);
    const auto canonical = info.canonicalFilePath();
    return canonical.isEmpty() ? info.absoluteFilePath() : canonical;
}

QString saveProfile(const QString& path, const QString& profileName)
{
    auto configuration = cutemac::config::ConfigurationManager::defaultMacPlusConfiguration();
    configuration.profileName = profileName;
    (void)cutemac::config::ConfigurationManager().saveTomlFile(path, configuration);
    return onDisk(path);
}

} // namespace

int main()
{
    // Redirect the managed profile directory away from the developer's real
    // configuration before anything touches ConfigurationManager.
    QStandardPaths::setTestModeEnabled(true);
    QCoreApplication::setOrganizationName(QStringLiteral("friedkiwi"));
    QCoreApplication::setApplicationName(QStringLiteral("CuteMacProfileResolverTests"));

    const cutemac::config::ConfigurationManager manager;
    const auto profileDirectory = cutemac::config::ConfigurationManager::profileDirectoryPath();
    QDir(profileDirectory).removeRecursively();
    if (!QDir().mkpath(profileDirectory)) {
        std::cerr << "FAIL: could not create the test profile directory\n";
        return 1;
    }

    bool ok = true;

    const auto plusPath = saveProfile(manager.profilePathForName(QStringLiteral("Mac Plus")), QStringLiteral("Mac Plus"));
    const auto buildPath = saveProfile(manager.profilePathForName(QStringLiteral("System 7 Build")), QStringLiteral("System 7 Build"));
    saveProfile(QDir(profileDirectory).filePath(QStringLiteral("twin-a.toml")), QStringLiteral("Twin"));
    saveProfile(QDir(profileDirectory).filePath(QStringLiteral("twin-b.toml")), QStringLiteral("Twin"));

    QTemporaryDir loose;
    const auto loosePath = saveProfile(loose.filePath(QStringLiteral("portable.toml")), QStringLiteral("Portable"));

    using cutemac::config::ProfileResolution;
    const auto resolve = [](const QString& request, cutemac::config::ResolvedProfile& resolved, QString& detail) {
        return cutemac::config::resolveProfile(request, resolved, &detail);
    };

    cutemac::config::ResolvedProfile resolved;
    QString detail;

    ok &= expect(resolve(loosePath, resolved, detail) == ProfileResolution::Ok
            && resolved.path == loosePath
            && resolved.configuration.profileName == QStringLiteral("Portable"),
        "an explicit profile file path outside the managed directory must resolve");

    ok &= expect(resolve(QStringLiteral("Mac Plus"), resolved, detail) == ProfileResolution::Ok
            && resolved.path == plusPath,
        "an exact profile name must resolve to its managed profile file");

    ok &= expect(resolve(QStringLiteral("mAc pLuS"), resolved, detail) == ProfileResolution::Ok
            && resolved.path == plusPath,
        "profile name matching must be case insensitive");

    ok &= expect(resolve(QStringLiteral("System_7_Build"), resolved, detail) == ProfileResolution::Ok
            && resolved.path == buildPath,
        "the slugged profile file base must resolve as a name");

    ok &= expect(resolve(QStringLiteral("Twin"), resolved, detail) == ProfileResolution::Ambiguous
            && detail.contains(QStringLiteral("twin-a.toml"))
            && detail.contains(QStringLiteral("twin-b.toml")),
        "duplicate profile names must be reported as ambiguous with their candidates");

    ok &= expect(resolve(QStringLiteral("No Such Profile"), resolved, detail) == ProfileResolution::NotFound
            && detail == profileDirectory,
        "an unknown profile must report the managed profile directory");

    ok &= expect(resolve(QString(), resolved, detail) == ProfileResolution::NotFound,
        "an empty request must not resolve");

    ok &= expect(resolve(QStringLiteral("   "), resolved, detail) == ProfileResolution::NotFound,
        "a whitespace-only request must not resolve");

    const auto malformedPath = loose.filePath(QStringLiteral("malformed.toml"));
    QFile malformed(malformedPath);
    ok &= expect(malformed.open(QIODevice::WriteOnly | QIODevice::Truncate), "malformed fixture open failed");
    malformed.write("[machine\nid =");
    malformed.close();
    ok &= expect(resolve(malformedPath, resolved, detail) == ProfileResolution::Unreadable
            && detail == onDisk(malformedPath),
        "a malformed profile file must be reported as unreadable");

    ok &= expect(cutemac::config::profileResolutionMessage(ProfileResolution::Ok, QStringLiteral("x"), QString()).isEmpty(),
        "a successful resolution must have no error message");
    ok &= expect(!cutemac::config::profileResolutionMessage(ProfileResolution::NotFound, QStringLiteral("x"), profileDirectory).isEmpty(),
        "a failed resolution must produce a user-facing message");

    QDir(profileDirectory).removeRecursively();
    return ok ? 0 : 1;
}
