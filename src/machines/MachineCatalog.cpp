#include "cutemac/machines/MachineCatalog.h"

namespace cutemac::machines {

QVector<MachineProfile> MachineCatalog::supportedMachines()
{
    return {
        {
            QStringLiteral("mac-plus"),
            QStringLiteral("Macintosh Plus"),
            CpuFamily::M68k,
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
        },
        {
            QStringLiteral("mac-iicx"),
            QStringLiteral("Macintosh IIcx"),
            CpuFamily::M68k,
            QStringLiteral("68030"),
            {
                QStringLiteral("device.adb.bus"),
                QStringLiteral("device.scsi.bus"),
                QStringLiteral("device.video"),
            },
        },
        {
            QStringLiteral("quadra-800"),
            QStringLiteral("Macintosh Quadra 800"),
            CpuFamily::M68k,
            QStringLiteral("68040"),
            {
                QStringLiteral("device.adb.bus"),
                QStringLiteral("device.scsi.bus"),
                QStringLiteral("device.video"),
            },
        },
        {
            QStringLiteral("powermac-6100"),
            QStringLiteral("Power Macintosh 6100"),
            CpuFamily::PowerPc,
            QStringLiteral("PowerPC 601"),
            {
                QStringLiteral("device.adb.bus"),
                QStringLiteral("device.scsi.bus"),
                QStringLiteral("device.video"),
            },
        },
    };
}

} // namespace cutemac::machines
