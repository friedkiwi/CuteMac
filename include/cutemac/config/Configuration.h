#pragma once

#include <QDir>
#include <QString>
#include <QStringList>
#include <QVector>
#include <optional>

namespace cutemac::config {

enum class RuntimeSpeed {
    Realtime,
    Unlimited,
};

enum class ScsiDeviceType {
    HardDisk,
    CdRom,
};

enum class NuBusDeviceType {
    MacintoshIIVideo,
    CuteMacVideo,
};

struct IwmDeviceConfiguration {
    QString imagePath;
    bool readOnly = false;
};

struct ScsiDeviceConfiguration {
    int id = 0;
    ScsiDeviceType type = ScsiDeviceType::HardDisk;
    QString imagePath;
    bool readOnly = false;
};

struct NuBusDeviceConfiguration {
    int slot = 9;
    NuBusDeviceType type = NuBusDeviceType::CuteMacVideo;
    QString declarationRomPath;
    int width = 640;
    int height = 480;
    int depth = 8;
    int vramMiB = 4;
    bool acceleration = true;
};

[[nodiscard]] QString runtimeSpeedName(RuntimeSpeed speed);
[[nodiscard]] RuntimeSpeed runtimeSpeedFromName(const QString& name);

class Configuration {
public:
    QString profileName;
    QString machineId;
    QString romPath;
    QString nvramPath;
    QString diskPath;
    QString floppyPath;
    QVector<IwmDeviceConfiguration> iwmDevices;
    QVector<ScsiDeviceConfiguration> scsiDevices;
    QVector<NuBusDeviceConfiguration> nubusDevices;
    int ramSizeKiB = 4096;
    int cyclesPerFrame = 130560;
    RuntimeSpeed runtimeSpeed = RuntimeSpeed::Unlimited;
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
