#include "cutemac/devices/adb/AdbBus.h"

namespace cutemac::devices::adb {

QString AdbBus::id() const
{
    return QStringLiteral("device.adb.bus");
}

void AdbBus::reset()
{
}

} // namespace cutemac::devices::adb
