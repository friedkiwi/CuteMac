#include "cutemac/config/Configuration.h"

#include <QFile>
#include <QTextStream>

namespace cutemac::config {

std::optional<Configuration> ConfigurationManager::loadTomlFile(const QString& path) const
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return std::nullopt;
    }

    // Placeholder until a TOML parser is selected. This preserves the public
    // boundary without pretending ad hoc parsing is emulator configuration.
    Q_UNUSED(file);
    return Configuration {};
}

bool ConfigurationManager::saveTomlFile(const QString& path, const Configuration& configuration) const
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
        return false;
    }

    QTextStream stream(&file);
    stream << "[machine]\n";
    stream << "id = \"" << configuration.machineId << "\"\n";
    stream << "rom_path = \"" << configuration.romPath << "\"\n";
    stream << "disk_path = \"" << configuration.diskPath << "\"\n";
    return true;
}

} // namespace cutemac::config
