#pragma once

#include <QVector>

#include "cutemac/machines/MachineProfile.h"

namespace cutemac::machines {

class MachineCatalog {
public:
    [[nodiscard]] static QVector<MachineProfile> supportedMachines();
};

} // namespace cutemac::machines
