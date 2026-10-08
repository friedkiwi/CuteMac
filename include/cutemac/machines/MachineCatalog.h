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
    [[nodiscard]] static QVector<int> nubusSlots(const QString& machineId);
    [[nodiscard]] static bool hasDevice(const QString& machineId, const QString& deviceId);
    [[nodiscard]] static bool hasDevicePrefix(const QString& machineId, const QString& prefix);
};

} // namespace cutemac::machines
