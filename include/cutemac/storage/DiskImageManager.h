#pragma once

#include <QString>
#include <QStringList>
#include <QVector>

namespace cutemac::storage {

enum class DiskImageType {
    Floppy,
    CdRom,
    HardDisk,
};

struct DiskImageEntry {
    QString path;
    DiskImageType type = DiskImageType::HardDisk;
    qint64 sizeBytes = 0;
    QString volumeIdentifier;
};

class DiskImageManager {
public:
    explicit DiskImageManager(QString libraryPath = {});

    // The process-wide manager over the user's image library. A scan walks the
    // library, probes every image for its volume identifier, and rewrites
    // library.toml, so it runs once at startup and afterwards only when the
    // user asks for it with a Refresh button. Sharing one instance also means
    // an import or a newly created image is immediately visible everywhere.
    static DiskImageManager& shared();

    [[nodiscard]] QString libraryPath() const;
    [[nodiscard]] QVector<DiskImageEntry> images() const;
    [[nodiscard]] QVector<DiskImageEntry> images(DiskImageType type) const;
    [[nodiscard]] QStringList collections() const;

    [[nodiscard]] bool refresh();
    [[nodiscard]] bool createCollection(const QString& relativePath);
    [[nodiscard]] bool importImage(const QString& sourcePath, DiskImageType type, QString* importedPath = nullptr,
        const QString& collection = {});
    [[nodiscard]] bool importImage(const QString& sourcePath, QString* importedPath = nullptr,
        const QString& collection = {});
    [[nodiscard]] bool importImages(const QStringList& sourcePaths, DiskImageType type, QStringList* importedPaths = nullptr,
        const QString& collection = {});
    [[nodiscard]] bool importImages(const QStringList& sourcePaths, QStringList* importedPaths = nullptr,
        const QString& collection = {});
    [[nodiscard]] bool exportImage(const QString& imagePath, const QString& destinationPath) const;
    [[nodiscard]] bool createImage(const QString& path, DiskImageType type, qint64 sizeBytes);
    [[nodiscard]] bool forgetImage(const QString& path);

    [[nodiscard]] static bool createBlankImage(const QString& path, qint64 sizeBytes);
    [[nodiscard]] static QString typeName(DiskImageType type);
    [[nodiscard]] static QString typeKey(DiskImageType type);
    [[nodiscard]] static DiskImageType detectType(const QString& path);

private:
    [[nodiscard]] bool loadCatalog();
    [[nodiscard]] bool saveCatalog() const;
    void registerImage(const QString& path, DiskImageType type);

    QString m_libraryPath;
    QVector<DiskImageEntry> m_images;
};

} // namespace cutemac::storage
