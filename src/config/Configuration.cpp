#include "cutemac/config/Configuration.h"

#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QStandardPaths>

#include <toml++/toml.hpp>

#include <sstream>

#include "cutemac/machines/MachineCatalog.h"

namespace cutemac::config {

namespace {

QString fromTomlString(const std::string& value)
{
    return QString::fromUtf8(value.data(), static_cast<qsizetype>(value.size()));
}

std::string toTomlString(const QString& value)
{
    const auto utf8 = value.toUtf8();
    return std::string(utf8.constData(), static_cast<std::size_t>(utf8.size()));
}

QString safeProfileFileBase(QString profileName)
{
    profileName = profileName.trimmed();
    if (profileName.isEmpty()) {
        profileName = QStringLiteral("Mac Plus");
    }

    profileName.replace(QRegularExpression(QStringLiteral(R"([^A-Za-z0-9._-]+)")), QStringLiteral("_"));
    profileName.replace(QRegularExpression(QStringLiteral(R"(^_+|_+$)")), QString());
    return profileName.isEmpty() ? QStringLiteral("Mac_Plus") : profileName;
}

} // namespace

QString runtimeSpeedName(RuntimeSpeed speed)
{
    switch (speed) {
    case RuntimeSpeed::Unlimited:
        return QStringLiteral("unlimited");
    case RuntimeSpeed::Realtime:
    default:
        return QStringLiteral("realtime");
    }
}

RuntimeSpeed runtimeSpeedFromName(const QString& name)
{
    return name.compare(QStringLiteral("unlimited"), Qt::CaseInsensitive) == 0
        ? RuntimeSpeed::Unlimited
        : RuntimeSpeed::Realtime;
}

QStringList Configuration::enabledRomPatches() const
{
    QStringList patches;
    if (skipRamPatternTest) {
        patches.append(machineId == QStringLiteral("mac-iicx")
                ? QStringLiteral("maciicx.skip_ram_pattern_test")
                : QStringLiteral("macplus.skip_ram_pattern_test"));
    }
    return patches;
}

QString ConfigurationManager::configRootPath()
{
    const auto path = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    return path.isEmpty() ? QDir::homePath() + QStringLiteral("/.config/CuteMac") : path;
}

QString ConfigurationManager::profileDirectoryPath()
{
    return QDir(configRootPath()).filePath(QStringLiteral("profiles"));
}

QString ConfigurationManager::romDirectoryPath()
{
    const auto path = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    return QDir(path.isEmpty() ? configRootPath() : path).filePath(QStringLiteral("roms"));
}

QString ConfigurationManager::diskImageDirectoryPath()
{
    const auto path = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    return QDir(path.isEmpty() ? configRootPath() : path).filePath(QStringLiteral("disk-images"));
}

Configuration ConfigurationManager::defaultMacPlusConfiguration()
{
    Configuration configuration;
    configuration.profileName = QStringLiteral("Mac Plus");
    configuration.machineId = QStringLiteral("mac-plus");
    configuration.ramSizeKiB = 4096;
    configuration.cyclesPerFrame = 130560;
    configuration.iwmDevices.append(IwmDeviceConfiguration {});
    return configuration;
}

bool ConfigurationManager::ensureDirectories() const
{
    QDir dir;
    return dir.mkpath(profileDirectoryPath())
        && dir.mkpath(romDirectoryPath())
        && dir.mkpath(diskImageDirectoryPath());
}

QVector<QString> ConfigurationManager::profileFilePaths() const
{
    const QDir directory(profileDirectoryPath());
    const auto entries = directory.entryInfoList({ QStringLiteral("*.toml") }, QDir::Files, QDir::Name);

    QVector<QString> paths;
    paths.reserve(entries.size());
    for (const auto& entry : entries) {
        paths.append(entry.absoluteFilePath());
    }
    return paths;
}

QString ConfigurationManager::profilePathForName(const QString& profileName) const
{
    return QDir(profileDirectoryPath()).filePath(safeProfileFileBase(profileName) + QStringLiteral(".toml"));
}

std::optional<Configuration> ConfigurationManager::loadTomlFile(const QString& path) const
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return std::nullopt;
    }

    auto configuration = defaultMacPlusConfiguration();
    configuration.iwmDevices.clear();
    try {
        const auto contents = file.readAll();
        const auto document = toml::parse(std::string_view(contents.constData(), static_cast<std::size_t>(contents.size())));
        configuration.profileName = fromTomlString(document["name"].value_or<std::string>(""));
        configuration.machineId = fromTomlString(document["machine"]["id"].value_or<std::string>(""));
        if (configuration.machineId == QStringLiteral("powermac-6100")) {
            configuration.machineId = QStringLiteral("powermac-8100");
        }
        configuration.romPath = fromTomlString(document["storage"]["rom_path"].value_or<std::string>(""));
        configuration.nvramPath = fromTomlString(document["storage"]["nvram_path"].value_or<std::string>(""));
        configuration.diskPath = fromTomlString(document["storage"]["disk_path"].value_or<std::string>(""));
        configuration.floppyPath = fromTomlString(document["storage"]["floppy_path"].value_or<std::string>(""));
        if (const auto sizeKiB = document["machine"]["ram_size_kib"].value<std::int64_t>()) {
            configuration.ramSizeKiB = static_cast<int>(*sizeKiB);
        } else if (const auto sizeMiB = document["machine"]["ram_size_mib"].value<std::int64_t>()) {
            configuration.ramSizeKiB = static_cast<int>(*sizeMiB) * 1024;
        } else if (!machines::MachineCatalog::isValidRamSize(configuration.machineId, configuration.ramSizeKiB)) {
            const auto machine = machines::MachineCatalog::find(configuration.machineId);
            if (!machine || machine->supportedRamSizesKiB.isEmpty()) return std::nullopt;
            configuration.ramSizeKiB = machine->supportedRamSizesKiB.first();
        }
        if (!machines::MachineCatalog::isValidRamSize(configuration.machineId, configuration.ramSizeKiB)) {
            return std::nullopt;
        }
        configuration.cyclesPerFrame = static_cast<int>(document["machine"]["cycles_per_frame"].value_or<std::int64_t>(configuration.cyclesPerFrame));
        configuration.runtimeSpeed = runtimeSpeedFromName(fromTomlString(document["runtime"]["speed"].value_or<std::string>("unlimited")));
        configuration.skipRamPatternTest = document["rom_patches"]["skip_ram_pattern_test"].value_or(false);

        if (const auto* devices = document["iwm"]["drives"].as_array()) {
            for (const auto& node : *devices) {
                if (const auto* drive = node.as_table()) {
                    configuration.iwmDevices.append({
                        fromTomlString((*drive)["image_path"].value_or<std::string>("")),
                        (*drive)["read_only"].value_or(false),
                    });
                }
            }
        }
        if (const auto* devices = document["scsi"]["devices"].as_array()) {
            for (const auto& node : *devices) {
                if (const auto* device = node.as_table()) {
                    const auto type = fromTomlString((*device)["type"].value_or<std::string>("hard_disk"));
                    configuration.scsiDevices.append({
                        static_cast<int>((*device)["id"].value_or<std::int64_t>(0)),
                        type == QStringLiteral("cd_rom") ? ScsiDeviceType::CdRom : ScsiDeviceType::HardDisk,
                        fromTomlString((*device)["image_path"].value_or<std::string>("")),
                        (*device)["read_only"].value_or(false),
                    });
                }
            }
        }
        if (const auto* devices = document["nubus"]["devices"].as_array()) {
            for (const auto& node : *devices) {
                if (const auto* device = node.as_table()) {
                    const auto type = fromTomlString((*device)["type"].value_or<std::string>("cutemac_video"));
                    configuration.nubusDevices.append({
                        static_cast<int>((*device)["slot"].value_or<std::int64_t>(9)),
                        type == QStringLiteral("apple_m2_video") ? NuBusDeviceType::MacintoshIIVideo : NuBusDeviceType::CuteMacVideo,
                        fromTomlString((*device)["declaration_rom_path"].value_or<std::string>("")),
                        static_cast<int>((*device)["width"].value_or<std::int64_t>(640)),
                        static_cast<int>((*device)["height"].value_or<std::int64_t>(480)),
                        static_cast<int>((*device)["depth"].value_or<std::int64_t>(8)),
                        static_cast<int>((*device)["vram_mib"].value_or<std::int64_t>(4)),
                        (*device)["acceleration"].value_or(true),
                        (*device)["absolute_pointer"].value_or(true),
                    });
                }
            }
        }
        if (const auto* devices = document["serial"]["devices"].as_array()) {
            for (const auto& node : *devices) {
                if (const auto* device = node.as_table()) {
                    configuration.serialDevices.append({
                        static_cast<int>((*device)["channel"].value_or<std::int64_t>(1)),
                        SerialDeviceType::ImageWriterII,
                        fromTomlString((*device)["output_directory"].value_or<std::string>("")),
                    });
                }
            }
        }
    } catch (const toml::parse_error&) {
        return std::nullopt;
    }

    if (configuration.profileName.isEmpty()) {
        configuration.profileName = QFileInfo(path).baseName();
    }
    if (configuration.machineId.isEmpty()) {
        configuration.machineId = QStringLiteral("mac-plus");
    }
    if (configuration.iwmDevices.isEmpty()) {
        configuration.iwmDevices.append({ configuration.floppyPath, false });
    } else {
        configuration.floppyPath = configuration.iwmDevices.first().imagePath;
    }
    if (configuration.scsiDevices.isEmpty() && !configuration.diskPath.isEmpty()) {
        configuration.scsiDevices.append({ 0, ScsiDeviceType::HardDisk, configuration.diskPath, false });
    } else if (!configuration.scsiDevices.isEmpty()) {
        configuration.diskPath = configuration.scsiDevices.first().imagePath;
    }

    return configuration;
}

bool ConfigurationManager::saveTomlFile(const QString& path, const Configuration& configuration) const
{
    if (!machines::MachineCatalog::isValidRamSize(configuration.machineId, configuration.ramSizeKiB)) {
        return false;
    }
    (void)ensureDirectories();

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
        return false;
    }

    toml::array iwmDevices;
    for (const auto& drive : configuration.iwmDevices) {
        iwmDevices.push_back(toml::table {
            { "image_path", toTomlString(drive.imagePath) },
            { "read_only", drive.readOnly },
        });
    }
    toml::array scsiDevices;
    for (const auto& device : configuration.scsiDevices) {
        scsiDevices.push_back(toml::table {
            { "id", device.id },
            { "type", device.type == ScsiDeviceType::CdRom ? "cd_rom" : "hard_disk" },
            { "image_path", toTomlString(device.imagePath) },
            { "read_only", device.readOnly },
        });
    }
    toml::array nubusDevices;
    for (const auto& device : configuration.nubusDevices) {
        nubusDevices.push_back(toml::table {
            { "slot", device.slot },
            { "type", device.type == NuBusDeviceType::MacintoshIIVideo ? "apple_m2_video" : "cutemac_video" },
            { "width", device.width },
            { "height", device.height },
            { "depth", device.depth },
            { "vram_mib", device.vramMiB },
            { "acceleration", device.acceleration },
            { "absolute_pointer", device.absolutePointer },
        });
    }
    toml::array serialDevices;
    for (const auto& device : configuration.serialDevices) {
        serialDevices.push_back(toml::table {
            { "channel", device.channel },
            { "type", "imagewriter_ii" },
            { "output_directory", toTomlString(device.outputDirectory) },
        });
    }

    toml::table document {
        { "name", toTomlString(configuration.profileName) },
        { "version", 3 },
        { "machine", toml::table {
                         { "id", toTomlString(configuration.machineId) },
                         { "ram_size_kib", configuration.ramSizeKiB },
                         { "cycles_per_frame", configuration.cyclesPerFrame },
                     } },
        { "storage", toml::table {
                         { "nvram_path", toTomlString(configuration.nvramPath) },
                         { "disk_path", toTomlString(configuration.diskPath) },
                         { "floppy_path", toTomlString(configuration.floppyPath) },
                     } },
        { "runtime", toml::table {
                         { "speed", toTomlString(runtimeSpeedName(configuration.runtimeSpeed)) },
                     } },
        { "iwm", toml::table { { "drives", std::move(iwmDevices) } } },
        { "scsi", toml::table { { "devices", std::move(scsiDevices) } } },
        { "nubus", toml::table { { "devices", std::move(nubusDevices) } } },
        { "serial", toml::table { { "devices", std::move(serialDevices) } } },
        { "rom_patches", toml::table {
                             { "skip_ram_pattern_test", configuration.skipRamPatternTest },
                         } },
    };
    std::ostringstream stream;
    stream << document;
    const auto serialized = stream.str();
    return file.write(serialized.data(), static_cast<qint64>(serialized.size())) == static_cast<qint64>(serialized.size());
}

} // namespace cutemac::config
