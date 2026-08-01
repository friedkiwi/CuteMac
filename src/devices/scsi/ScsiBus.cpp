#include "cutemac/devices/scsi/ScsiBus.h"

namespace cutemac::devices::scsi {

QString ScsiBus::id() const
{
    return QStringLiteral("device.scsi.bus");
}

void ScsiBus::reset()
{
}

} // namespace cutemac::devices::scsi
