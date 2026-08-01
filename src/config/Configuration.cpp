#include "cutemac/config/Configuration.h"

#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QStandardPaths>

#include <toml++/toml.hpp>

#include <sstream>

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

QStringList Configuration::enabledRomPatches() const
{
    QStringList patches;
    if (skipRamPatternTest) {
        patches.append(QStringLiteral("macplus.skip_ram_pattern_test"));
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
    configuration.ramSizeMiB = 4;
    configuration.cyclesPerFrame = 130560;
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
    try {
        const auto contents = file.readAll();
        const auto document = toml::parse(std::string_view(contents.constData(), static_cast<std::size_t>(contents.size())));
        configuration.profileName = fromTomlString(document["name"].value_or<std::string>(""));
        configuration.machineId = fromTomlString(document["machine"]["id"].value_or<std::string>(""));
        configuration.romPath = fromTomlString(document["storage"]["rom_path"].value_or<std::string>(""));
        configuration.diskPath = fromTomlString(document["storage"]["disk_path"].value_or<std::string>(""));
        configuration.floppyPath = fromTomlString(document["storage"]["floppy_path"].value_or<std::string>(""));
        configuration.ramSizeMiB = static_cast<int>(document["machine"]["ram_size_mib"].value_or<std::int64_t>(configuration.ramSizeMiB));
        configuration.cyclesPerFrame = static_cast<int>(document["machine"]["cycles_per_frame"].value_or<std::int64_t>(configuration.cyclesPerFrame));
        configuration.skipRamPatternTest = document["rom_patches"]["skip_ram_pattern_test"].value_or(false);
    } catch (const toml::parse_error&) {
        return std::nullopt;
    }

    if (configuration.profileName.isEmpty()) {
        configuration.profileName = QFileInfo(path).baseName();
    }
    if (configuration.machineId.isEmpty()) {
        configuration.machineId = QStringLiteral("mac-plus");
    }

    return configuration;
}

bool ConfigurationManager::saveTomlFile(const QString& path, const Configuration& configuration) const
{
    (void)ensureDirectories();

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
        return false;
    }

    toml::table document {
        { "name", toTomlString(configuration.profileName) },
        { "version", 1 },
        { "machine", toml::table {
                         { "id", toTomlString(configuration.machineId) },
                         { "ram_size_mib", configuration.ramSizeMiB },
                         { "cycles_per_frame", configuration.cyclesPerFrame },
                     } },
        { "storage", toml::table {
                         { "rom_path", toTomlString(configuration.romPath) },
                         { "disk_path", toTomlString(configuration.diskPath) },
                         { "floppy_path", toTomlString(configuration.floppyPath) },
                     } },
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
