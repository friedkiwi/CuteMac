#pragma once

#include <cstdint>

#include <QByteArray>
#include <QString>

#include "cutemac/devices/scsi/ScsiTarget.h"

namespace cutemac::devices::scsi {

class ScsiCdRomDevice final : public ScsiTarget {
public:
    [[nodiscard]] bool loadImage(const QString& path);
    void eject();
    void acknowledgeMediaChange();

    [[nodiscard]] bool ready() const override;
    [[nodiscard]] bool selectable() const override { return true; }
    [[nodiscard]] QString imagePath() const { return m_imagePath; }
    [[nodiscard]] std::uint64_t activityCounter() const { return m_activityCounter; }
    [[nodiscard]] ScsiCommandResult executeCommand(const QByteArray& cdb, const QByteArray& dataOut) override;

private:
    [[nodiscard]] std::uint32_t blockCount() const;
    [[nodiscard]] ScsiCommandResult read(std::uint32_t lba, std::uint32_t blocks);
    [[nodiscard]] ScsiCommandResult readAppleRaw(std::uint32_t lba, std::uint32_t blocks);
    [[nodiscard]] ScsiCommandResult modeSelect(bool tenByte, const QByteArray& parameters);
    [[nodiscard]] ScsiCommandResult inquiry(std::uint8_t allocationLength) const;
    [[nodiscard]] ScsiCommandResult requestSense(std::uint8_t allocationLength);
    [[nodiscard]] ScsiCommandResult modeSense(bool tenByte, bool disableBlockDescriptors,
        std::uint8_t pageCode, std::uint16_t allocationLength) const;
    [[nodiscard]] ScsiCommandResult readCapacity() const;
    [[nodiscard]] ScsiCommandResult readToc(const QByteArray& cdb) const;
    [[nodiscard]] ScsiCommandResult good(QByteArray data = {}) const;
    [[nodiscard]] ScsiCommandResult checkCondition(std::uint8_t senseKey, std::uint8_t asc = 0);

    QByteArray m_image;
    QString m_imagePath;
    std::uint8_t m_senseKey = 0;
    std::uint8_t m_additionalSenseCode = 0;
    bool m_unitAttention = false;
    std::uint32_t m_logicalBlockSize = 2048;
    std::uint64_t m_activityCounter = 0;
};

} // namespace cutemac::devices::scsi
