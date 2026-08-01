#include "cutemac/storage/DiskImageManager.h"

#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>

#include <cstdlib>
#include <iostream>

namespace {

void require(bool condition, const char* message)
{
    if (!condition) {
        std::cerr << message << '\n';
        std::exit(1);
    }
}

} // namespace

int main()
{
    using cutemac::storage::DiskImageManager;
    using cutemac::storage::DiskImageType;

    QTemporaryDir temporary;
    require(temporary.isValid(), "temporary directory should be available");
    const auto libraryPath = temporary.filePath(QStringLiteral("library"));
    DiskImageManager manager(libraryPath);

    const auto floppyPath = temporary.filePath(QStringLiteral("blank-800k.dsk"));
    require(manager.createImage(floppyPath, DiskImageType::Floppy, 800 * 1024), "800K floppy creation should succeed");
    require(QFileInfo(floppyPath).size() == 800 * 1024, "800K floppy should have the requested size");

    const auto hdPath = temporary.filePath(QStringLiteral("blank.hda"));
    require(manager.createImage(hdPath, DiskImageType::HardDisk, 20LL * 1024 * 1024), "hard disk creation should succeed");
    require(QFileInfo(hdPath).size() == 20LL * 1024 * 1024, "hard disk should have the requested size");
    require(manager.images(DiskImageType::Floppy).size() == 1, "floppy picker view should only contain floppies");
    require(manager.images(DiskImageType::HardDisk).size() == 1, "hard disk picker view should only contain hard disks");
    require(manager.images(DiskImageType::CdRom).isEmpty(), "CD picker view should not contain other media");

    const auto sourceIso = temporary.filePath(QStringLiteral("install.iso"));
    require(DiskImageManager::createBlankImage(sourceIso, 4096), "test CD image creation should succeed");
    QString importedPath;
    require(manager.importImage(sourceIso, DiskImageType::CdRom, &importedPath), "CD import should succeed");
    require(importedPath.startsWith(libraryPath), "imports should be copied into the library");
    require(manager.images(DiskImageType::CdRom).size() == 1, "imported CD should retain its explicit type");

    DiskImageManager reloaded(libraryPath);
    require(reloaded.images(DiskImageType::CdRom).size() == 1, "catalog type should persist across manager instances");
    const auto exportedPath = temporary.filePath(QStringLiteral("exported.iso"));
    require(reloaded.exportImage(importedPath, exportedPath), "export should succeed");
    require(QFileInfo(exportedPath).size() == 4096, "export should preserve image contents");

    return 0;
}
