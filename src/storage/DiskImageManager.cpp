#include "cutemac/storage/DiskImageManager.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>

#include <toml++/toml.hpp>

#include <algorithm>
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
        m_images.append({ absolutePath, type, QFileInfo(absolutePath).size() });
    } else {
        found->type = type;
        found->sizeBytes = QFileInfo(absolutePath).size();
    }
}

} // namespace cutemac::storage
