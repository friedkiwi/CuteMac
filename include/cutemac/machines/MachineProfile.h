#pragma once

#include <QString>
#include <QStringList>
#include <QVector>

namespace cutemac::machines {

class MachineProfile {
public:
    QString id;
    QString displayName;
    QString cpuModel;
    QStringList reusableDevices;
    QVector<int> supportedRamSizesKiB;
};

} // namespace cutemac::machines
