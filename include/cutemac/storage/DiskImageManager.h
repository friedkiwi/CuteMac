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
};

class DiskImageManager {
public:
    explicit DiskImageManager(QString libraryPath = {});

    [[nodiscard]] QString libraryPath() const;
    [[nodiscard]] QVector<DiskImageEntry> images() const;
    [[nodiscard]] QVector<DiskImageEntry> images(DiskImageType type) const;

    [[nodiscard]] bool refresh();
    [[nodiscard]] bool importImage(const QString& sourcePath, DiskImageType type, QString* importedPath = nullptr);
    [[nodiscard]] bool importImages(const QStringList& sourcePaths, DiskImageType type, QStringList* importedPaths = nullptr);
    [[nodiscard]] bool exportImage(const QString& imagePath, const QString& destinationPath) const;
    [[nodiscard]] bool createImage(const QString& path, DiskImageType type, qint64 sizeBytes);
    [[nodiscard]] bool forgetImage(const QString& path);

    [[nodiscard]] static bool createBlankImage(const QString& path, qint64 sizeBytes);
    [[nodiscard]] static QString typeName(DiskImageType type);
    [[nodiscard]] static QString typeKey(DiskImageType type);

private:
    [[nodiscard]] bool loadCatalog();
    [[nodiscard]] bool saveCatalog() const;
    void registerImage(const QString& path, DiskImageType type);

    QString m_libraryPath;
    QVector<DiskImageEntry> m_images;
};

} // namespace cutemac::storage
