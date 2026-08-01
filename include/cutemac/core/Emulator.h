#pragma once

#include <QString>

#include "cutemac/machines/MachineProfile.h"

namespace cutemac::core {

class Emulator {
public:
    void configure(machines::MachineProfile profile);
    void reset();

    [[nodiscard]] QString selectedMachineId() const;

private:
    machines::MachineProfile m_profile;
};

} // namespace cutemac::core
