#include <QApplication>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QMessageBox>

#include <memory>

#if CUTEMAC_ENABLE_PANIC_DUMP
#include "cutemac/debug/HostLogRing.h"
#endif
#include "cutemac/config/Configuration.h"
#include "cutemac/config/ProfileResolver.h"
#include "cutemac/rom/RomCatalog.h"
#include "cutemac/session/SessionWindowManager.h"
#include "cutemac/storage/DiskImageManager.h"
#include "cutemac/ui/ProfileManagerWindow.h"

int main(int argc, char* argv[])
{
#if CUTEMAC_ENABLE_PANIC_DUMP
    // Before the QApplication so startup warnings are captured too.
    cutemac::debug::HostLogRing::install();
#endif
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("friedkiwi"));
    QCoreApplication::setApplicationName(QStringLiteral("CuteMac"));
    QCoreApplication::setApplicationVersion(QStringLiteral(CUTEMAC_VERSION));

    // Both catalogs scan a whole user directory, so build them once here, after
    // the application name has settled the standard paths they resolve. Windows
    // and dialogs then read cached state and never scan on a button press; a
    // Refresh button is what rebuilds them.
    (void)cutemac::config::ConfigurationManager().ensureDirectories();
    (void)cutemac::rom::RomCatalog::shared();
    (void)cutemac::storage::DiskImageManager::shared();

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral(
        "CuteMac classic Macintosh emulator.\n\n"
        "Run without arguments to open the session manager. Pass a profile name or a\n"
        "path to a profile TOML file to start that machine directly."));
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addPositionalArgument(QStringLiteral("profile"),
        QStringLiteral("Profile name or path to a CuteMac TOML profile."), QStringLiteral("[profile]"));
    // Retained so existing scripts and launchers that pass --profile keep working.
    const QCommandLineOption profileOption(QStringLiteral("profile"),
        QStringLiteral("Profile name or path to a CuteMac TOML profile."), QStringLiteral("profile"));
    parser.addOption(profileOption);
    parser.process(app);

    QString request;
    if (parser.isSet(profileOption)) {
        request = parser.value(profileOption);
    } else if (!parser.positionalArguments().isEmpty()) {
        request = parser.positionalArguments().constFirst();
    }

    cutemac::session::SessionWindowManager sessions;

    if (request.isEmpty()) {
        auto window = std::make_unique<cutemac::ui::ProfileManagerWindow>(sessions);
        sessions.setProfileManagerWindow(window.get());
        window->show();
        return app.exec();
    }

    cutemac::config::ResolvedProfile resolved;
    QString detail;
    const auto resolution = cutemac::config::resolveProfile(request, resolved, &detail);
    if (resolution != cutemac::config::ProfileResolution::Ok) {
        // This is a GUI-subsystem binary, so a console message would be lost on
        // Windows. Report startup failures the same way on every platform.
        QMessageBox::critical(nullptr, QStringLiteral("CuteMac"),
            cutemac::config::profileResolutionMessage(resolution, request, detail));
        return 2;
    }

    QString error;
    if (!sessions.open(resolved.configuration, resolved.path, &error)) {
        QMessageBox::critical(nullptr, QStringLiteral("CuteMac"),
            error.isEmpty() ? QStringLiteral("Could not start the emulator session.") : error);
        return 2;
    }

    return app.exec();
}
