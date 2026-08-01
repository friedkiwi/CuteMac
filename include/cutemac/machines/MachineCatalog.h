#pragma once

#include <optional>

#include <QString>
#include <QVector>

#include "cutemac/machines/MachineProfile.h"

namespace cutemac::machines {

class MachineCatalog {
public:
    [[nodiscard]] static QVector<MachineProfile> supportedMachines();
    [[nodiscard]] static std::optional<MachineProfile> find(const QString& machineId);
    [[nodiscard]] static bool isValidRamSize(const QString& machineId, int sizeKiB);
};

} // namespace cutemac::machines
