#pragma once

#include <QString>
#include <QStringList>
#include <QVector>

namespace cutemac::machines {

enum class CpuFamily {
    M68k,
    PowerPc,
};

class MachineProfile {
public:
    QString id;
    QString displayName;
    CpuFamily cpuFamily = CpuFamily::M68k;
    QString cpuModel;
    QStringList reusableDevices;
    QVector<int> supportedRamSizesKiB;
};

} // namespace cutemac::machines
