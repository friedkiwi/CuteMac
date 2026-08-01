#pragma once

#include <cstdint>
#include <QByteArray>

namespace cutemac::devices::scsi {

struct ScsiCommandResult {
    QByteArray data;
    std::uint8_t status = 0;
    std::uint8_t message = 0;
    std::uint8_t senseKey = 0;
};

class ScsiTarget {
public:
    virtual ~ScsiTarget() = default;

    [[nodiscard]] virtual bool ready() const = 0;
    [[nodiscard]] virtual bool selectable() const { return ready(); }
    [[nodiscard]] virtual ScsiCommandResult executeCommand(const QByteArray& cdb, const QByteArray& dataOut) = 0;
};

} // namespace cutemac::devices::scsi
