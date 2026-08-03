#include "cutemac/storage/DiskImageManager.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>

#include <algorithm>
#include <cstdint>
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

void putBe16(QByteArray& bytes, qsizetype offset, std::uint16_t value)
{
    bytes[offset] = static_cast<char>(value >> 8);
    bytes[offset + 1] = static_cast<char>(value);
}

void putBe32(QByteArray& bytes, qsizetype offset, std::uint32_t value)
{
    bytes[offset] = static_cast<char>(value >> 24);
    bytes[offset + 1] = static_cast<char>(value >> 16);
    bytes[offset + 2] = static_cast<char>(value >> 8);
    bytes[offset + 3] = static_cast<char>(value);
}

void putHfsVolumeName(QByteArray& bytes, qsizetype filesystemOffset, const QByteArray& name)
{
    const auto block = filesystemOffset + 1024;
    putBe16(bytes, block, 0x4244);
    bytes[block + 36] = static_cast<char>(name.size());
    std::copy(name.cbegin(), name.cend(), bytes.begin() + block + 37);
}

void writeImage(const QString& path, const QByteArray& bytes)
{
    QFile file(path);
    require(file.open(QIODevice::WriteOnly), "volume test image should open");
    require(file.write(bytes) == bytes.size(), "volume test image should be written");
}

QString volumeFor(const cutemac::storage::DiskImageManager& manager, const QString& fileName)
{
    const auto images = manager.images();
    const auto found = std::find_if(images.cbegin(), images.cend(), [&](const auto& image) {
        return QFileInfo(image.path).fileName() == fileName;
    });
    return found == images.cend() ? QString() : found->volumeIdentifier;
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
    const auto highDensityFloppyPath = temporary.filePath(QStringLiteral("blank-1440k.dsk"));
    require(manager.createImage(highDensityFloppyPath, DiskImageType::Floppy, 1440 * 1024), "1.44 MB floppy creation should succeed");
    require(QFileInfo(highDensityFloppyPath).size() == 1440 * 1024, "1.44 MB floppy should have the requested size");

    const auto hdPath = temporary.filePath(QStringLiteral("blank.hda"));
    require(manager.createImage(hdPath, DiskImageType::HardDisk, 20LL * 1024 * 1024), "hard disk creation should succeed");
    require(QFileInfo(hdPath).size() == 20LL * 1024 * 1024, "hard disk should have the requested size");
    require(manager.images(DiskImageType::Floppy).size() == 2, "floppy picker view should only contain floppies");
    require(manager.images(DiskImageType::HardDisk).size() == 1, "hard disk picker view should only contain hard disks");
    require(manager.images(DiskImageType::CdRom).isEmpty(), "CD picker view should not contain other media");

    const auto largeDsk = temporary.filePath(QStringLiteral("hard-disk-with-dsk-extension.dsk"));
    require(DiskImageManager::createBlankImage(largeDsk, 20LL * 1024 * 1024), "large DSK fixture should be created");
    require(DiskImageManager::detectType(largeDsk) == DiskImageType::HardDisk,
        "large DSK files must not be misidentified as floppies by extension alone");
    require(DiskImageManager::detectType(floppyPath) == DiskImageType::Floppy,
        "raw floppy size must identify floppy media");
    QByteArray diskCopyImg(84 + 1440 * 1024, '\0');
    putBe32(diskCopyImg, 64, 1440 * 1024);
    const auto diskCopyImgPath = temporary.filePath(QStringLiteral("installation-boot.img"));
    writeImage(diskCopyImgPath, diskCopyImg);
    require(DiskImageManager::detectType(diskCopyImgPath) == DiskImageType::Floppy,
        "Disk Copy 4.2 floppies with an .img suffix must be recognized structurally");
    const auto migrationLibrary = temporary.filePath(QStringLiteral("migration-library"));
    require(QDir().mkpath(migrationLibrary), "migration library should be created");
    DiskImageManager migrationManager(migrationLibrary);
    QString legacyDiskCopyPath;
    require(migrationManager.importImage(diskCopyImgPath, DiskImageType::HardDisk, &legacyDiskCopyPath),
        "legacy catalog fixture should accept the old hard-disk classification");
    require(migrationManager.refresh(), "legacy catalog refresh should succeed");
    require(migrationManager.images(DiskImageType::Floppy).size() == 1
            && migrationManager.images(DiskImageType::HardDisk).isEmpty(),
        "refresh must migrate a structurally valid Disk Copy floppy out of the hard-disk catalog");

    const auto sourceIso = temporary.filePath(QStringLiteral("install.iso"));
    require(DiskImageManager::createBlankImage(sourceIso, 4096), "test CD image creation should succeed");
    QString importedPath;
    require(manager.importImage(sourceIso, DiskImageType::CdRom, &importedPath), "CD import should succeed");
    require(importedPath.startsWith(libraryPath), "imports should be copied into the library");
    require(manager.images(DiskImageType::CdRom).size() == 1, "imported CD should retain its explicit type");

    const auto autoFloppy = temporary.filePath(QStringLiteral("auto-floppy.img"));
    require(DiskImageManager::createBlankImage(autoFloppy, 800 * 1024), "automatic floppy fixture should be created");
    QString autoImportedPath;
    require(manager.importImage(autoFloppy, &autoImportedPath), "automatic image import should succeed");
    const auto autoEntry = manager.images(DiskImageType::Floppy);
    require(std::any_of(autoEntry.cbegin(), autoEntry.cend(), [&](const auto& entry) { return entry.path == autoImportedPath; }),
        "automatic import must catalog the detected media type");

    const auto sourceFloppy1 = temporary.filePath(QStringLiteral("install-1.dsk"));
    const auto sourceFloppy2 = temporary.filePath(QStringLiteral("install-2.dsk"));
    require(DiskImageManager::createBlankImage(sourceFloppy1, 800 * 1024), "first batch source should be created");
    require(DiskImageManager::createBlankImage(sourceFloppy2, 1440 * 1024), "second batch source should be created");
    QStringList importedFloppies;
    require(manager.importImages({ sourceFloppy1, sourceFloppy2 }, DiskImageType::Floppy, &importedFloppies), "multiple images should import together");
    require(importedFloppies.size() == 2, "batch import should return every copied image");
    require(std::all_of(importedFloppies.cbegin(), importedFloppies.cend(), [&](const auto& path) { return path.startsWith(libraryPath); }),
        "every batch import should be copied into the designated library folder");
    require(manager.images(DiskImageType::Floppy).size() == 5, "all imported floppies should be cataloged");

    require(manager.createCollection(QStringLiteral("System 6/Install Disks")), "nested collections should be created");
    require(manager.collections().contains(QStringLiteral("System 6")), "parent collections should be listed");
    require(manager.collections().contains(QStringLiteral("System 6/Install Disks")), "nested collections should be listed");
    require(!manager.createCollection(QStringLiteral("../outside")), "collections must remain inside the image library");
    const auto collectionSource = temporary.filePath(QStringLiteral("system-tools.dsk"));
    require(DiskImageManager::createBlankImage(collectionSource, 800 * 1024), "collection import source should be created");
    QString collectionImage;
    require(manager.importImage(collectionSource, DiskImageType::Floppy, &collectionImage, QStringLiteral("System 6/Install Disks")),
        "images should import into a collection");
    require(QFileInfo(collectionImage).absolutePath()
            == QFileInfo(QDir(libraryPath).filePath(QStringLiteral("System 6/Install Disks/placeholder"))).absolutePath(),
        "collection imports should use the selected nested directory");

    DiskImageManager reloaded(libraryPath);
    require(reloaded.images(DiskImageType::CdRom).size() == 1, "catalog type should persist across manager instances");
    require(reloaded.images(DiskImageType::Floppy).size() == 6, "nested image catalog entries should persist across manager instances");
    const auto discoveredIso = QDir(libraryPath).filePath(QStringLiteral("System 6/discovered.iso"));
    require(DiskImageManager::createBlankImage(discoveredIso, 2048), "nested discovery image should be created");
    require(reloaded.refresh(), "nested library refresh should succeed");
    require(reloaded.images(DiskImageType::CdRom).size() == 2, "refresh should discover uncataloged images in collections");
    const auto exportedPath = temporary.filePath(QStringLiteral("exported.iso"));
    require(reloaded.exportImage(importedPath, exportedPath), "export should succeed");
    require(QFileInfo(exportedPath).size() == 4096, "export should preserve image contents");

    const auto volumeLibrary = temporary.filePath(QStringLiteral("volume-library"));
    require(QDir().mkpath(volumeLibrary), "volume test library should be created");
    QByteArray rawHfs(800 * 1024, '\0');
    putHfsVolumeName(rawHfs, 0, "System Tools");
    writeImage(QDir(volumeLibrary).filePath(QStringLiteral("raw.dsk")), rawHfs);

    QByteArray diskCopy(84 + 800 * 1024, '\0');
    putBe32(diskCopy, 64, 800 * 1024);
    putHfsVolumeName(diskCopy, 84, "Utilities 1");
    writeImage(QDir(volumeLibrary).filePath(QStringLiteral("wrapped.dc42")), diskCopy);

    QByteArray partitioned(16 * 512, '\0');
    putBe16(partitioned, 0, 0x4552);
    putBe16(partitioned, 2, 512);
    putBe16(partitioned, 512, 0x504d);
    putBe32(partitioned, 516, 1);
    putBe32(partitioned, 520, 4);
    const QByteArray partitionType("Apple_HFS");
    std::copy(partitionType.cbegin(), partitionType.cend(), partitioned.begin() + 512 + 48);
    putHfsVolumeName(partitioned, 4 * 512, "Macintosh HD");
    writeImage(QDir(volumeLibrary).filePath(QStringLiteral("partitioned.hda")), partitioned);

    QByteArray iso(17 * 2048, '\0');
    iso[16 * 2048] = 1;
    std::copy_n("CD001", 5, iso.begin() + 16 * 2048 + 1);
    std::copy_n("SYSTEM_7_CD", 11, iso.begin() + 16 * 2048 + 40);
    writeImage(QDir(volumeLibrary).filePath(QStringLiteral("install.iso")), iso);

    DiskImageManager volumeManager(volumeLibrary);
    require(volumeFor(volumeManager, QStringLiteral("raw.dsk")) == QStringLiteral("System Tools"), "raw HFS volume names should be detected");
    require(volumeFor(volumeManager, QStringLiteral("wrapped.dc42")) == QStringLiteral("Utilities 1"), "Disk Copy volume names should be detected");
    require(volumeFor(volumeManager, QStringLiteral("partitioned.hda")) == QStringLiteral("Macintosh HD"), "partitioned HFS volume names should be detected");
    require(volumeFor(volumeManager, QStringLiteral("install.iso")) == QStringLiteral("SYSTEM_7_CD"), "ISO volume identifiers should be detected");

    return 0;
}
