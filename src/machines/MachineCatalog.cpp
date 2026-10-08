#include "cutemac/machines/MachineCatalog.h"

#include <algorithm>

#include "cutemac/machines/macplus/MacPlusMachine.h"
#include "cutemac/machines/maciicx/MacIIcxMachine.h"
#include "cutemac/machines/quadra700/Quadra700Machine.h"

namespace cutemac::machines {

QVector<MachineProfile> MachineCatalog::supportedMachines()
{
    using Model = macplus::MacPlusMachine::Model;
    return {
        macplus::MacPlusMachine::configurationProfile(Model::Macintosh128K),
        macplus::MacPlusMachine::configurationProfile(Model::Macintosh512K),
        macplus::MacPlusMachine::configurationProfile(Model::Macintosh512Ke),
        macplus::MacPlusMachine::configurationProfile(Model::MacintoshPlus),
        maciicx::MacIIcxMachine::configurationProfile(),
        quadra700::Quadra700Machine::configurationProfile(),
    };
}

std::optional<MachineProfile> MachineCatalog::find(const QString& machineId)
{
    const auto machines = supportedMachines();
    const auto it = std::find_if(machines.cbegin(), machines.cend(), [&](const auto& machine) {
        return machine.id == machineId;
    });
    return it == machines.cend() ? std::nullopt : std::optional<MachineProfile>(*it);
}

bool MachineCatalog::isValidRamSize(const QString& machineId, int sizeKiB)
{
    const auto machine = find(machineId);
    return machine && machine->supportedRamSizesKiB.contains(sizeKiB);
}

QVector<int> MachineCatalog::nubusSlots(const QString& machineId)
{
    const auto machine = find(machineId);
    return machine ? machine->nubusSlots : QVector<int> {};
}

bool MachineCatalog::hasDevice(const QString& machineId, const QString& deviceId)
{
    const auto machine = find(machineId);
    return machine && machine->reusableDevices.contains(deviceId);
}

bool MachineCatalog::hasDevicePrefix(const QString& machineId, const QString& prefix)
{
    const auto machine = find(machineId);
    if (!machine) return false;
    return std::any_of(machine->reusableDevices.cbegin(), machine->reusableDevices.cend(),
        [&prefix](const QString& device) { return device.startsWith(prefix); });
}

} // namespace cutemac::machines
