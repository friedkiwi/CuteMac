#pragma once

#include <QDir>
#include <QString>
#include <QStringList>
#include <QVector>
#include <optional>

namespace cutemac::config {

class Configuration {
public:
    QString profileName;
    QString machineId;
    QString romPath;
    QString diskPath;
    QString floppyPath;
    int ramSizeMiB = 4;
    int cyclesPerFrame = 130560;
    bool skipRamPatternTest = false;

    [[nodiscard]] QStringList enabledRomPatches() const;
};

class ConfigurationManager {
public:
    [[nodiscard]] static QString configRootPath();
    [[nodiscard]] static QString profileDirectoryPath();
    [[nodiscard]] static QString romDirectoryPath();
    [[nodiscard]] static QString diskImageDirectoryPath();

    [[nodiscard]] static Configuration defaultMacPlusConfiguration();

    [[nodiscard]] bool ensureDirectories() const;
    [[nodiscard]] QVector<QString> profileFilePaths() const;
    [[nodiscard]] QString profilePathForName(const QString& profileName) const;

    [[nodiscard]] std::optional<Configuration> loadTomlFile(const QString& path) const;
    [[nodiscard]] bool saveTomlFile(const QString& path, const Configuration& configuration) const;
};

} // namespace cutemac::config
