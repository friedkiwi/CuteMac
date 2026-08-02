#include "cutemac/storage/DiskImageManager.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>

#include <toml++/toml.hpp>

#include <algorithm>
#include <cstdint>
#include <sstream>

#include "cutemac/config/Configuration.h"

namespace cutemac::storage {
namespace {

std::string toString(const QString& value)
{
    const auto bytes = value.toUtf8();
    return { bytes.constData(), static_cast<std::size_t>(bytes.size()) };
}

QString fromString(const std::string& value)
{
    return QString::fromUtf8(value.data(), static_cast<qsizetype>(value.size()));
}

DiskImageType typeFromKey(const QString& key)
{
    if (key == QStringLiteral("floppy")) return DiskImageType::Floppy;
    if (key == QStringLiteral("cd_rom")) return DiskImageType::CdRom;
    return DiskImageType::HardDisk;
}

DiskImageType inferType(const QFileInfo& file)
{
    const auto suffix = file.suffix().toLower();
    if (suffix == QStringLiteral("iso") || suffix == QStringLiteral("cdr")) return DiskImageType::CdRom;
    if (suffix == QStringLiteral("dsk") || suffix == QStringLiteral("dc42") || suffix == QStringLiteral("image")) return DiskImageType::Floppy;
    if (file.size() == 400 * 1024 || file.size() == 800 * 1024 || file.size() == 1440 * 1024) return DiskImageType::Floppy;
    return DiskImageType::HardDisk;
}

std::uint16_t readBe16(const QByteArray& bytes, qsizetype offset)
{
    if (offset < 0 || offset + 2 > bytes.size()) return 0;
    return (static_cast<std::uint16_t>(static_cast<std::uint8_t>(bytes[offset])) << 8)
        | static_cast<std::uint8_t>(bytes[offset + 1]);
}

std::uint32_t readBe32(const QByteArray& bytes, qsizetype offset)
{
    if (offset < 0 || offset + 4 > bytes.size()) return 0;
    return (static_cast<std::uint32_t>(static_cast<std::uint8_t>(bytes[offset])) << 24)
        | (static_cast<std::uint32_t>(static_cast<std::uint8_t>(bytes[offset + 1])) << 16)
        | (static_cast<std::uint32_t>(static_cast<std::uint8_t>(bytes[offset + 2])) << 8)
        | static_cast<std::uint8_t>(bytes[offset + 3]);
}

QByteArray readAt(QFile& file, qint64 offset, qint64 size)
{
    if (offset < 0 || size < 0 || offset > file.size() || size > file.size() - offset || !file.seek(offset)) return {};
    return file.read(size);
}

QString classicVolumeName(const QByteArray& volumeBlock)
{
    const auto signature = readBe16(volumeBlock, 0);
    if (signature != 0x4244 && signature != 0xd2d7) return {};
    if (volumeBlock.size() <= 36) return {};
    const auto length = std::min<int>(static_cast<std::uint8_t>(volumeBlock[36]), 27);
    if (37 + length > volumeBlock.size()) return {};
    return QString::fromLatin1(volumeBlock.constData() + 37, length).trimmed();
}

QString classicVolumeNameAt(QFile& file, qint64 filesystemOffset)
{
    return classicVolumeName(readAt(file, filesystemOffset + 1024, 64));
}

QString partitionedClassicVolumeName(QFile& file)
{
    const auto descriptor = readAt(file, 0, 512);
    if (readBe16(descriptor, 0) != 0x4552) return {};
    const auto blockSize = readBe16(descriptor, 2);
    if (blockSize < 512 || blockSize > 4096) return {};
    const auto firstPartition = readAt(file, blockSize, blockSize);
    if (readBe16(firstPartition, 0) != 0x504d) return {};
    const auto partitionCount = std::min<std::uint32_t>(readBe32(firstPartition, 4), 256);
    for (std::uint32_t index = 0; index < partitionCount; ++index) {
        const auto partition = index == 0 ? firstPartition : readAt(file, static_cast<qint64>(index + 1) * blockSize, blockSize);
        if (readBe16(partition, 0) != 0x504d) break;
        const auto type = QByteArray(partition.constData() + 48, std::min<qsizetype>(32, partition.size() - 48));
        if (!type.startsWith("Apple_HFS")) continue;
        const auto startBlock = readBe32(partition, 8);
        const auto name = classicVolumeNameAt(file, static_cast<qint64>(startBlock) * blockSize);
        if (!name.isEmpty()) return name;
    }
    return {};
}

QString isoVolumeIdentifier(QFile& file)
{
    const auto descriptor = readAt(file, 16LL * 2048, 2048);
    if (descriptor.size() < 72 || static_cast<std::uint8_t>(descriptor[0]) != 1 || descriptor.mid(1, 5) != QByteArrayLiteral("CD001")) return {};
    auto identifier = descriptor.mid(40, 32);
    while (!identifier.isEmpty() && (identifier.back() == '\0' || identifier.back() == ' ')) identifier.chop(1);
    return QString::fromLatin1(identifier).trimmed();
}

QString detectVolumeIdentifier(const QString& path, DiskImageType type)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return {};
    if (type == DiskImageType::CdRom) {
        const auto identifier = isoVolumeIdentifier(file);
        if (!identifier.isEmpty()) return identifier;
    }
    auto identifier = classicVolumeNameAt(file, 0);
    if (!identifier.isEmpty()) return identifier;
    const auto diskCopyHeader = readAt(file, 0, 84);
    const auto dataSize = readBe32(diskCopyHeader, 64);
    const auto tagSize = readBe32(diskCopyHeader, 68);
    if (diskCopyHeader.size() == 84 && dataSize >= 3 * 512 && static_cast<qint64>(84) + dataSize + tagSize == file.size()) {
        identifier = classicVolumeNameAt(file, 84);
        if (!identifier.isEmpty()) return identifier;
    }
    return partitionedClassicVolumeName(file);
}

QString uniqueDestination(const QDir& directory, const QFileInfo& source)
{
    QString candidate = directory.filePath(source.fileName());
    for (int number = 2; QFileInfo::exists(candidate); ++number) {
        candidate = directory.filePath(QStringLiteral("%1-%2%3").arg(source.completeBaseName()).arg(number).arg(
            source.suffix().isEmpty() ? QString() : QLatin1Char('.') + source.suffix()));
    }
    return candidate;
}

bool normalizeCollectionPath(const QString& path, QString* normalized)
{
    const auto clean = QDir::cleanPath(path.trimmed());
    if (clean.isEmpty() || clean == QStringLiteral(".")) {
        *normalized = {};
        return true;
    }
    if (path.contains(QLatin1Char('\\')) || QDir::isAbsolutePath(clean) || clean == QStringLiteral("..")
        || clean.startsWith(QStringLiteral("../"))) return false;
    *normalized = clean;
    return true;
}

} // namespace

DiskImageManager::DiskImageManager(QString libraryPath)
    : m_libraryPath(libraryPath.isEmpty() ? config::ConfigurationManager::diskImageDirectoryPath() : std::move(libraryPath))
{
    (void)QDir().mkpath(m_libraryPath);
    (void)refresh();
}

QString DiskImageManager::libraryPath() const { return m_libraryPath; }

QVector<DiskImageEntry> DiskImageManager::images() const { return m_images; }

QVector<DiskImageEntry> DiskImageManager::images(DiskImageType type) const
{
    QVector<DiskImageEntry> result;
    std::copy_if(m_images.cbegin(), m_images.cend(), std::back_inserter(result), [type](const auto& image) { return image.type == type; });
    return result;
}

QStringList DiskImageManager::collections() const
{
    QStringList result;
    const QDir library(m_libraryPath);
    QDirIterator iterator(m_libraryPath, QDir::Dirs | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
    while (iterator.hasNext()) result.append(library.relativeFilePath(iterator.next()));
    result.sort(Qt::CaseInsensitive);
    return result;
}

bool DiskImageManager::refresh()
{
    const bool loaded = loadCatalog();
    m_images.erase(std::remove_if(m_images.begin(), m_images.end(), [](const auto& image) { return !QFileInfo(image.path).isFile(); }), m_images.end());

    const QDir directory(m_libraryPath);
    QDirIterator iterator(m_libraryPath, QDir::Files | QDir::Readable, QDirIterator::Subdirectories);
    while (iterator.hasNext()) {
        const QFileInfo file(iterator.next());
        if (file.fileName() == QStringLiteral("library.toml")) continue;
        const auto absolutePath = file.absoluteFilePath();
        const auto found = std::find_if(m_images.cbegin(), m_images.cend(), [&](const auto& image) { return image.path == absolutePath; });
        if (found == m_images.cend()) registerImage(absolutePath, inferType(file));
    }
    std::sort(m_images.begin(), m_images.end(), [&](const auto& left, const auto& right) {
        return directory.relativeFilePath(left.path).compare(directory.relativeFilePath(right.path), Qt::CaseInsensitive) < 0;
    });
    return saveCatalog() && loaded;
}

bool DiskImageManager::createCollection(const QString& relativePath)
{
    QString normalized;
    if (!normalizeCollectionPath(relativePath, &normalized) || normalized.isEmpty()) return false;
    return QDir(m_libraryPath).mkpath(normalized);
}

bool DiskImageManager::importImage(const QString& sourcePath, DiskImageType type, QString* importedPath, const QString& collection)
{
    const QFileInfo source(sourcePath);
    if (!source.isFile()) return false;
    QString normalized;
    if (!normalizeCollectionPath(collection, &normalized)) return false;
    QDir destinationDirectory(m_libraryPath);
    if (!normalized.isEmpty() && (!destinationDirectory.mkpath(normalized) || !destinationDirectory.cd(normalized))) return false;
    const auto destination = uniqueDestination(destinationDirectory, source);
    if (!QFile::copy(source.absoluteFilePath(), destination)) return false;
    registerImage(destination, type);
    if (!saveCatalog()) {
        QFile::remove(destination);
        return false;
    }
    if (importedPath != nullptr) *importedPath = destination;
    return true;
}

bool DiskImageManager::importImages(const QStringList& sourcePaths, DiskImageType type, QStringList* importedPaths,
    const QString& collection)
{
    if (sourcePaths.isEmpty()) return false;
    bool success = true;
    QStringList results;
    for (const auto& sourcePath : sourcePaths) {
        QString importedPath;
        if (importImage(sourcePath, type, &importedPath, collection)) results.append(importedPath);
        else success = false;
    }
    if (importedPaths != nullptr) *importedPaths = results;
    return success;
}

bool DiskImageManager::exportImage(const QString& imagePath, const QString& destinationPath) const
{
    if (imagePath.isEmpty() || destinationPath.isEmpty()) return false;
    if (QFileInfo(imagePath).absoluteFilePath() == QFileInfo(destinationPath).absoluteFilePath()) return true;
    if (QFileInfo::exists(destinationPath) && !QFile::remove(destinationPath)) return false;
    return QFile::copy(imagePath, destinationPath);
}

bool DiskImageManager::createImage(const QString& path, DiskImageType type, qint64 sizeBytes)
{
    if (type == DiskImageType::CdRom || !createBlankImage(path, sizeBytes)) return false;
    registerImage(QFileInfo(path).absoluteFilePath(), type);
    return saveCatalog();
}

bool DiskImageManager::forgetImage(const QString& path)
{
    const auto absolutePath = QFileInfo(path).absoluteFilePath();
    const auto oldSize = m_images.size();
    m_images.erase(std::remove_if(m_images.begin(), m_images.end(), [&](const auto& image) { return image.path == absolutePath; }), m_images.end());
    return oldSize != m_images.size() && saveCatalog();
}

bool DiskImageManager::createBlankImage(const QString& path, qint64 sizeBytes)
{
    if (path.isEmpty() || sizeBytes <= 0) return false;
    const QFileInfo info(path);
    if (!QDir().mkpath(info.absolutePath())) return false;
    QFile file(info.absoluteFilePath());
    return file.open(QIODevice::WriteOnly | QIODevice::Truncate) && file.resize(sizeBytes);
}

QString DiskImageManager::typeName(DiskImageType type)
{
    switch (type) {
    case DiskImageType::Floppy: return QStringLiteral("Floppy");
    case DiskImageType::CdRom: return QStringLiteral("CD-ROM");
    case DiskImageType::HardDisk: return QStringLiteral("Hard disk");
    }
    return {};
}

QString DiskImageManager::typeKey(DiskImageType type)
{
    switch (type) {
    case DiskImageType::Floppy: return QStringLiteral("floppy");
    case DiskImageType::CdRom: return QStringLiteral("cd_rom");
    case DiskImageType::HardDisk: return QStringLiteral("hard_disk");
    }
    return {};
}

bool DiskImageManager::loadCatalog()
{
    m_images.clear();
    QFile file(QDir(m_libraryPath).filePath(QStringLiteral("library.toml")));
    if (!file.exists()) return true;
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) return false;
    try {
        const auto bytes = file.readAll();
        const auto document = toml::parse(std::string_view(bytes.constData(), static_cast<std::size_t>(bytes.size())));
        if (const auto* images = document["images"].as_array()) {
            for (const auto& node : *images) {
                const auto* table = node.as_table();
                if (table == nullptr) continue;
                const auto relativePath = fromString((*table)["path"].value_or<std::string>(""));
                if (relativePath.isEmpty()) continue;
                registerImage(QDir(m_libraryPath).absoluteFilePath(relativePath),
                    typeFromKey(fromString((*table)["type"].value_or<std::string>("hard_disk"))));
            }
        }
    } catch (const toml::parse_error&) {
        return false;
    }
    return true;
}

bool DiskImageManager::saveCatalog() const
{
    toml::array images;
    const QDir directory(m_libraryPath);
    for (const auto& image : m_images) {
        images.push_back(toml::table {
            { "path", toString(directory.relativeFilePath(image.path)) },
            { "type", toString(typeKey(image.type)) },
        });
    }
    toml::table document { { "version", 1 }, { "images", std::move(images) } };
    std::ostringstream stream;
    stream << document;
    const auto serialized = stream.str();
    QSaveFile file(directory.filePath(QStringLiteral("library.toml")));
    return file.open(QIODevice::WriteOnly | QIODevice::Text)
        && file.write(serialized.data(), static_cast<qint64>(serialized.size())) == static_cast<qint64>(serialized.size())
        && file.commit();
}

void DiskImageManager::registerImage(const QString& path, DiskImageType type)
{
    const auto absolutePath = QFileInfo(path).absoluteFilePath();
    auto found = std::find_if(m_images.begin(), m_images.end(), [&](const auto& image) { return image.path == absolutePath; });
    if (found == m_images.end()) {
        m_images.append({ absolutePath, type, QFileInfo(absolutePath).size(), detectVolumeIdentifier(absolutePath, type) });
    } else {
        found->type = type;
        found->sizeBytes = QFileInfo(absolutePath).size();
        found->volumeIdentifier = detectVolumeIdentifier(absolutePath, type);
    }
}

} // namespace cutemac::storage
