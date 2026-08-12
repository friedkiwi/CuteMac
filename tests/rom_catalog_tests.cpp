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

bool writeFile(const QString& path, const QByteArray& contents)
{
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(contents) == contents.size();
}

QByteArray romOfSize(qsizetype size, const QByteArray& headerHex = {})
{
    QByteArray bytes(size, '\0');
    if (!headerHex.isEmpty()) bytes.replace(0, 4, QByteArray::fromHex(headerHex));
    return bytes;
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

    QTemporaryDir compactDirectory;
    QByteArray mac128k(64 * 1024, '\0');
    mac128k.replace(0, 4, QByteArray::fromHex("28ba61ce"));
    QFile mac128kFile(compactDirectory.filePath(QStringLiteral("macintosh.bin")));
    ok &= expect(mac128kFile.open(QIODevice::WriteOnly), "128K ROM fixture open");
    ok &= expect(mac128kFile.write(mac128k) == mac128k.size(), "128K ROM fixture write");
    mac128kFile.close();
    cutemac::rom::RomCatalog compactCatalog(compactDirectory.path());
    ok &= expect(!compactCatalog.pathForId(QStringLiteral("mac128k-28ba61ce")).isEmpty(),
        "ROM manager must identify the Macintosh 128K ROM independent of filename");

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
    configuration.machineId = QStringLiteral("mac-128k");
    ok &= expect(compactCatalog.missingRomNames(configuration).isEmpty(), "found 128K ROM must satisfy Macintosh 128K requirement");

    // A scan only reads files whose size some outstanding definition expects,
    // so make sure the files it now skips cannot change what it identifies.
    QTemporaryDir noisyDirectory;
    ok &= expect(writeFile(noisyDirectory.filePath(QStringLiteral("tiny.rom")), romOfSize(999)), "odd-size fixture");
    ok &= expect(writeFile(noisyDirectory.filePath(QStringLiteral("half-meg.rom")), romOfSize(512 * 1024)), "unknown-size fixture");
    ok &= expect(writeFile(noisyDirectory.filePath(QStringLiteral("blank-128k.rom")), romOfSize(128 * 1024)), "right-size wrong-content fixture");
    ok &= expect(writeFile(noisyDirectory.filePath(QStringLiteral("plus.rom")), romOfSize(128 * 1024, "4d1eeae1")), "v2 fixture");
    cutemac::rom::RomCatalog noisyCatalog(noisyDirectory.path());
    ok &= expect(noisyCatalog.pathForId(QStringLiteral("macplus-v2")).endsWith(QStringLiteral("plus.rom")),
        "files of an unknown size must not stop a real ROM from being identified");
    ok &= expect(noisyCatalog.pathForId(QStringLiteral("macplus-v1")).isEmpty(),
        "a file of the right size with the wrong header must not be identified");

    // What the Refresh button is for: a rescan picks up a ROM dropped into the
    // folder after the catalog was built.
    QTemporaryDir laterDirectory;
    cutemac::rom::RomCatalog laterCatalog(laterDirectory.path());
    ok &= expect(laterCatalog.pathForId(QStringLiteral("mac128k-28ba61ce")).isEmpty(), "empty folder must identify nothing");
    ok &= expect(writeFile(laterDirectory.filePath(QStringLiteral("added.rom")), romOfSize(64 * 1024, "28ba61ce")), "late fixture");
    ok &= expect(laterCatalog.pathForId(QStringLiteral("mac128k-28ba61ce")).isEmpty(), "a cached catalog must not rescan on its own");
    laterCatalog.refresh();
    ok &= expect(!laterCatalog.pathForId(QStringLiteral("mac128k-28ba61ce")).isEmpty(), "refresh must pick up a newly added ROM");

    ok &= expect(&cutemac::rom::RomCatalog::shared() == &cutemac::rom::RomCatalog::shared(),
        "the shared catalog must be one instance so a scan is not repeated per caller");
    return ok ? 0 : 1;
}
