#include "cutemac/machines/MachineCatalog.h"

#include <algorithm>

namespace cutemac::machines {

QVector<MachineProfile> MachineCatalog::supportedMachines()
{
    return {
        {
            QStringLiteral("mac-128k"),
            QStringLiteral("Macintosh 128K"),
            QStringLiteral("68000"),
            {
                QStringLiteral("device.via6522"),
                QStringLiteral("device.scc.z8530"),
                QStringLiteral("device.iwm"),
                QStringLiteral("device.video.compact-mac"),
                QStringLiteral("device.audio.compact-mac"),
            },
            { 128 },
        },
        {
            QStringLiteral("mac-512k"),
            QStringLiteral("Macintosh 512K"),
            QStringLiteral("68000"),
            {
                QStringLiteral("device.via6522"),
                QStringLiteral("device.scc.z8530"),
                QStringLiteral("device.iwm"),
                QStringLiteral("device.video.compact-mac"),
                QStringLiteral("device.audio.compact-mac"),
            },
            { 512 },
        },
        {
            QStringLiteral("mac-512ke"),
            QStringLiteral("Macintosh 512Ke"),
            QStringLiteral("68000"),
            {
                QStringLiteral("device.via6522"),
                QStringLiteral("device.scc.z8530"),
                QStringLiteral("device.iwm"),
                QStringLiteral("device.video.compact-mac"),
                QStringLiteral("device.audio.compact-mac"),
                QStringLiteral("device.rtc.pram"),
            },
            { 512 },
        },
        {
            QStringLiteral("mac-plus"),
            QStringLiteral("Macintosh Plus"),
            QStringLiteral("68000"),
            {
                QStringLiteral("device.via6522"),
                QStringLiteral("device.scc.z8530"),
                QStringLiteral("device.iwm"),
                QStringLiteral("device.scsi.ncr5380"),
                QStringLiteral("device.video.compact-mac"),
                QStringLiteral("device.audio.compact-mac"),
                QStringLiteral("device.rtc.pram"),
            },
            { 1024, 2560, 4096 },
        },
        {
            QStringLiteral("mac-iicx"),
            QStringLiteral("Macintosh IIcx"),
            QStringLiteral("68030"),
            {
                QStringLiteral("device.via6522.primary"),
                QStringLiteral("device.via6522.secondary"),
                QStringLiteral("device.scc.z8530"),
                QStringLiteral("device.adb.bus"),
                QStringLiteral("device.swim1"),
                QStringLiteral("device.scsi.ncr5380"),
                QStringLiteral("device.nubus"),
                QStringLiteral("device.audio.asc"),
                QStringLiteral("device.rtc.pram"),
            },
            { 1024, 2048, 4096, 5120, 8192, 16384, 17408, 20480, 32768, 65536, 66560, 69632, 81920, 131072 },
        },
        {
            QStringLiteral("quadra-700"),
            QStringLiteral("Macintosh Quadra 700"),
            QStringLiteral("68040"),
            {
                QStringLiteral("device.via6522.primary"),
                QStringLiteral("device.via6522.secondary"),
                QStringLiteral("device.scc.z8530"),
                QStringLiteral("device.adb.bus"),
                QStringLiteral("device.swim1"),
                QStringLiteral("device.scsi.ncr53c96"),
                QStringLiteral("device.nubus"),
                QStringLiteral("device.video.dafb"),
                QStringLiteral("device.audio.asc"),
                QStringLiteral("device.rtc.pram"),
            },
            { 4096, 8192 },
        },
        {
            QStringLiteral("quadra-800"),
            QStringLiteral("Macintosh Quadra 800"),
            QStringLiteral("68040"),
            {
                QStringLiteral("device.adb.bus"),
                QStringLiteral("device.scc.z8530"),
                QStringLiteral("device.scsi.bus"),
                QStringLiteral("device.video"),
            },
            { 8192, 12288, 24576, 40960, 73728, 139264 },
        },
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
    if (machineId == QStringLiteral("mac-iicx")) return { 9, 10, 11 };
    if (machineId == QStringLiteral("quadra-700")) return { 13, 14 };
    return {};
}

} // namespace cutemac::machines
