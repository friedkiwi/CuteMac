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
    AppleDisplayCard824,
    CuteMacVideo,
    CuteMacVideoAccelerated,
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
    int vramKiB = 4096;
    bool acceleration = true;
    bool absolutePointer = true;
};

[[nodiscard]] bool isCuteMacVideoDevice(NuBusDeviceType type);
[[nodiscard]] int cuteMacVideoFramebufferLimitBytes();
[[nodiscard]] int framebufferStrideBytes(int width, int depth);
[[nodiscard]] bool isValidNuBusDeviceConfiguration(const NuBusDeviceConfiguration& device);

enum class SerialDeviceType {
    ImageWriterII,
    HayesModem,
    NullModem,
};

enum class SerialTcpMode {
    Listen,
    Dial,
};

struct SerialPhonebookEntry {
    QString number;
    QString target;
    bool telnet = false;
};

struct SerialSlipConfiguration {
    bool enabled = true;
    QString localIp = QStringLiteral("172.16.0.1");
    QString remoteIp = QStringLiteral("172.16.0.2");
    int mtu = 1006;
};

struct SerialDeviceConfiguration {
    int channel = 1; // SCC A=0 (modem), B=1 (printer)
    SerialDeviceType type = SerialDeviceType::ImageWriterII;
    QString outputDirectory;
    bool directTcpDialing = false;
    QVector<SerialPhonebookEntry> phonebook;
    SerialSlipConfiguration slip;
    SerialTcpMode tcpMode = SerialTcpMode::Listen;
    QString tcpHost = QStringLiteral("127.0.0.1");
    int tcpPort = 0;
};

[[nodiscard]] QVector<SerialPhonebookEntry> defaultSerialModemPhonebook();
[[nodiscard]] bool isValidSerialDeviceConfiguration(const SerialDeviceConfiguration& device);

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
    QVector<SerialDeviceConfiguration> serialDevices;
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
