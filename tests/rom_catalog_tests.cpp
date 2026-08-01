#include <QFile>
#include <QTemporaryDir>

#include <algorithm>
#include <iostream>

#include "cutemac/rom/RomCatalog.h"

namespace {
bool expect(bool condition, const char* message)
{
    if (!condition) std::cerr << "FAIL: " << message << '\n';
    return condition;
}
}

int main()
{
    bool ok = true;
    QTemporaryDir directory;
    QByteArray v2(128 * 1024, '\0');
    v2.replace(0, 4, QByteArray::fromHex("4d1eeae1"));
    QFile file(directory.filePath(QStringLiteral("anything.dat")));
    ok &= expect(file.open(QIODevice::WriteOnly), "fixture open");
    ok &= expect(file.write(v2) == v2.size(), "fixture write");
    file.close();

    cutemac::rom::RomCatalog catalog(directory.path());
    ok &= expect(!catalog.pathForId(QStringLiteral("macplus-v2")).isEmpty(), "content scan must identify v2 independent of filename");
    ok &= expect(catalog.pathForId(QStringLiteral("macplus-v1")).isEmpty(), "different revision must remain missing");
    ok &= expect(catalog.pathForId(QStringLiteral("cutemac-video")) == QStringLiteral("(embedded)"), "embedded ROM must always be available");

    const auto definitions = cutemac::rom::RomCatalog::definitions();
    const auto powerMacRom = std::find_if(definitions.cbegin(), definitions.cend(), [](const auto& definition) {
        return definition.id == QStringLiteral("powermac-pdm-9feb69b3");
    });
    ok &= expect(powerMacRom != definitions.cend()
            && powerMacRom->ownerId == QStringLiteral("powermac-8100")
            && powerMacRom->revision == QStringLiteral("9FEB69B3"),
        "the shared launch Power Macintosh ROM must belong to the 8100 target");

    cutemac::config::Configuration configuration;
    configuration.machineId = QStringLiteral("mac-plus");
    ok &= expect(catalog.missingRomNames(configuration).isEmpty(), "found compatible revision must satisfy machine requirement");
    ok &= expect(catalog.warningForConfiguration(configuration).contains(QStringLiteral("older ROM revision")), "v2 must produce old-revision warning");
    return ok ? 0 : 1;
}
