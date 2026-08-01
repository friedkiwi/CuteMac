#include "cutemac/config/Configuration.h"

#include <QRegularExpression>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
#include <QTextStream>

namespace cutemac::config {

namespace {

QString tomlEscape(QString value)
{
    value.replace(QLatin1Char('\\'), QStringLiteral("\\\\"));
    value.replace(QLatin1Char('"'), QStringLiteral("\\\""));
    return value;
}

QString tomlStringValue(const QString& contents, const QString& key)
{
    const QRegularExpression expression(QStringLiteral(R"toml(^\s*%1\s*=\s*"((?:\\.|[^"])*)"\s*$)toml").arg(QRegularExpression::escape(key)),
        QRegularExpression::MultilineOption);
    const auto match = expression.match(contents);
    if (!match.hasMatch()) {
        return {};
    }

    auto value = match.captured(1);
    value.replace(QStringLiteral("\\\""), QStringLiteral("\""));
    value.replace(QStringLiteral("\\\\"), QStringLiteral("\\"));
    return value;
}

int tomlIntValue(const QString& contents, const QString& key, int fallback)
{
    const QRegularExpression expression(QStringLiteral(R"(^\s*%1\s*=\s*([0-9]+)\s*$)").arg(QRegularExpression::escape(key)),
        QRegularExpression::MultilineOption);
    const auto match = expression.match(contents);
    if (!match.hasMatch()) {
        return fallback;
    }

    bool ok = false;
    const auto value = match.captured(1).toInt(&ok);
    return ok ? value : fallback;
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
    configuration.cyclesPerFrame = 200000;
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

    const auto contents = QString::fromUtf8(file.readAll());
    auto configuration = defaultMacPlusConfiguration();
    configuration.profileName = tomlStringValue(contents, QStringLiteral("name"));
    configuration.machineId = tomlStringValue(contents, QStringLiteral("id"));
    configuration.romPath = tomlStringValue(contents, QStringLiteral("rom_path"));
    configuration.diskPath = tomlStringValue(contents, QStringLiteral("disk_path"));
    configuration.ramSizeMiB = tomlIntValue(contents, QStringLiteral("ram_size_mib"), configuration.ramSizeMiB);
    configuration.cyclesPerFrame = tomlIntValue(contents, QStringLiteral("cycles_per_frame"), configuration.cyclesPerFrame);

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

    QTextStream stream(&file);
    stream << "name = \"" << tomlEscape(configuration.profileName) << "\"\n";
    stream << "version = 1\n\n";
    stream << "[machine]\n";
    stream << "id = \"" << tomlEscape(configuration.machineId) << "\"\n";
    stream << "ram_size_mib = " << configuration.ramSizeMiB << "\n";
    stream << "cycles_per_frame = " << configuration.cyclesPerFrame << "\n\n";
    stream << "[storage]\n";
    stream << "rom_path = \"" << tomlEscape(configuration.romPath) << "\"\n";
    stream << "disk_path = \"" << tomlEscape(configuration.diskPath) << "\"\n";
    return true;
}

} // namespace cutemac::config
