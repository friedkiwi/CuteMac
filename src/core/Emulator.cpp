#include "cutemac/core/Emulator.h"

namespace cutemac::core {

void Emulator::configure(machines::MachineProfile profile)
{
    m_profile = std::move(profile);
}

void Emulator::reset()
{
}

QString Emulator::selectedMachineId() const
{
    return m_profile.id;
}

} // namespace cutemac::core
