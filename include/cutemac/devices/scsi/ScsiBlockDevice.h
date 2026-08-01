#pragma once

#include <cstdint>
#include <QByteArray>
#include <QString>

#include "cutemac/devices/scsi/ScsiTarget.h"

namespace cutemac::devices::scsi {

class ScsiBlockDevice final : public ScsiTarget {
public:
    [[nodiscard]] bool loadImage(const QString& path, bool readOnly = false);
    void eject();

    [[nodiscard]] bool ready() const override;
    [[nodiscard]] QString imagePath() const;
    [[nodiscard]] ScsiCommandResult executeCommand(const QByteArray& cdb, const QByteArray& dataOut) override;

private:
    [[nodiscard]] std::uint32_t blockCount() const;
    [[nodiscard]] QByteArray readBlocks(std::uint32_t lba, std::uint32_t blocks);
    [[nodiscard]] bool writeBlocks(std::uint32_t lba, const QByteArray& bytes);
    [[nodiscard]] ScsiCommandResult good(QByteArray data = {}) const;
    [[nodiscard]] ScsiCommandResult checkCondition(std::uint8_t senseKey);
    [[nodiscard]] ScsiCommandResult inquiry(bool evpd, std::uint8_t pageCode, std::uint8_t allocationLength);
    [[nodiscard]] ScsiCommandResult requestSense(std::uint8_t allocationLength) const;
    [[nodiscard]] ScsiCommandResult modeSense(std::uint8_t pageCode, std::uint8_t allocationLength);
    [[nodiscard]] ScsiCommandResult readCapacity() const;

    QByteArray m_image;
    QString m_imagePath;
    bool m_readOnly = false;
    std::uint8_t m_senseKey = 0;
};

} // namespace cutemac::devices::scsi
