#pragma once

#include <QString>
#include <optional>

namespace cutemac::config {

class Configuration {
public:
    QString machineId;
    QString romPath;
    QString diskPath;
};

class ConfigurationManager {
public:
    [[nodiscard]] std::optional<Configuration> loadTomlFile(const QString& path) const;
    [[nodiscard]] bool saveTomlFile(const QString& path, const Configuration& configuration) const;
};

} // namespace cutemac::config
