#include "cutemac/devices/scsi/ScsiBlockDevice.h"

#include <QFile>

#include <algorithm>

namespace cutemac::devices::scsi {

namespace {

constexpr std::uint32_t blockSize = 512;
constexpr std::uint8_t statusGood = 0x00;
constexpr std::uint8_t statusCheckCondition = 0x02;
constexpr std::uint8_t messageCommandComplete = 0x00;
constexpr std::uint8_t senseNoSense = 0x00;
constexpr std::uint8_t senseNotReady = 0x02;
constexpr std::uint8_t senseIllegalRequest = 0x05;

void appendBe32(QByteArray& bytes, std::uint32_t value)
{
    bytes.append(static_cast<char>(value >> 24));
    bytes.append(static_cast<char>(value >> 16));
    bytes.append(static_cast<char>(value >> 8));
    bytes.append(static_cast<char>(value));
}

QByteArray clipped(QByteArray bytes, std::uint8_t allocationLength)
{
    bytes.truncate(std::min<int>(allocationLength, bytes.size()));
    return bytes;
}

} // namespace

bool ScsiBlockDevice::loadImage(const QString& path, bool readOnly)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return false;
    }

    m_image = file.readAll();
    if (m_image.isEmpty()) {
        m_imagePath.clear();
        return false;
    }

    m_imagePath = path;
    m_readOnly = readOnly;
    m_senseKey = senseNoSense;
    return true;
}

void ScsiBlockDevice::eject()
{
    m_image.clear();
    m_imagePath.clear();
    m_senseKey = senseNotReady;
}

bool ScsiBlockDevice::ready() const
{
    return !m_image.isEmpty();
}

QString ScsiBlockDevice::imagePath() const
{
    return m_imagePath;
}

ScsiCommandResult ScsiBlockDevice::executeCommand(const QByteArray& cdb, const QByteArray& dataOut)
{
    if (cdb.isEmpty()) {
        return checkCondition(senseIllegalRequest);
    }

    const auto opcode = static_cast<std::uint8_t>(cdb[0]);
    if (!ready() && opcode != 0x03 && opcode != 0x12) {
        return checkCondition(senseNotReady);
    }

    switch (opcode) {
    case 0x00:
        return good();
    case 0x04:
        // The backing image is already a fixed-size block medium; formatting is completed by subsequent writes.
        return good();
    case 0x03:
        return requestSense(cdb.size() > 4 ? static_cast<std::uint8_t>(cdb[4]) : 4);
    case 0x08: {
        if (cdb.size() < 6) {
            return checkCondition(senseIllegalRequest);
        }
        const auto lba = ((static_cast<std::uint32_t>(static_cast<std::uint8_t>(cdb[1]) & 0x1f)) << 16)
            | (static_cast<std::uint32_t>(static_cast<std::uint8_t>(cdb[2])) << 8)
            | static_cast<std::uint8_t>(cdb[3]);
        const auto blocks = static_cast<std::uint8_t>(cdb[4]) == 0 ? 256U : static_cast<std::uint32_t>(static_cast<std::uint8_t>(cdb[4]));
        const auto data = readBlocks(lba, blocks);
        return m_senseKey == senseNoSense ? good(data) : checkCondition(m_senseKey);
    }
    case 0x0a: {
        if (cdb.size() < 6 || m_readOnly) {
            return checkCondition(senseIllegalRequest);
        }
        const auto lba = ((static_cast<std::uint32_t>(static_cast<std::uint8_t>(cdb[1]) & 0x1f)) << 16)
            | (static_cast<std::uint32_t>(static_cast<std::uint8_t>(cdb[2])) << 8)
            | static_cast<std::uint8_t>(cdb[3]);
        return writeBlocks(lba, dataOut) ? good() : checkCondition(senseIllegalRequest);
    }
    case 0x12:
        return inquiry(
            cdb.size() > 1 ? (static_cast<std::uint8_t>(cdb[1]) & 0x01) != 0 : false,
            cdb.size() > 2 ? static_cast<std::uint8_t>(cdb[2]) : 0,
            cdb.size() > 4 ? static_cast<std::uint8_t>(cdb[4]) : 36);
    case 0x15:
        // Geometry and format parameters are advisory for the fixed-size image.
        return good();
    case 0x1a:
        return modeSense(
            cdb.size() > 2 ? static_cast<std::uint8_t>(cdb[2]) & 0x3f : 0x3f,
            cdb.size() > 4 ? static_cast<std::uint8_t>(cdb[4]) : 4);
    case 0x25:
        return readCapacity();
    default:
        return checkCondition(senseIllegalRequest);
    }
}

std::uint32_t ScsiBlockDevice::blockCount() const
{
    return static_cast<std::uint32_t>(m_image.size() / blockSize);
}

QByteArray ScsiBlockDevice::readBlocks(std::uint32_t lba, std::uint32_t blocks)
{
    const auto offset = static_cast<qsizetype>(lba * blockSize);
    const auto length = static_cast<qsizetype>(blocks * blockSize);
    if (lba >= blockCount() || offset + length > m_image.size()) {
        m_senseKey = senseIllegalRequest;
        return {};
    }

    m_senseKey = senseNoSense;
    return m_image.mid(offset, length);
}

bool ScsiBlockDevice::writeBlocks(std::uint32_t lba, const QByteArray& bytes)
{
    const auto offset = static_cast<qsizetype>(lba * blockSize);
    if ((bytes.size() % blockSize) != 0 || lba >= blockCount() || offset + bytes.size() > m_image.size()) {
        m_senseKey = senseIllegalRequest;
        return false;
    }

    QFile file(m_imagePath);
    if (!file.open(QIODevice::ReadWrite) || !file.seek(offset) || file.write(bytes) != bytes.size()) {
        m_senseKey = senseIllegalRequest;
        return false;
    }
    std::copy(bytes.begin(), bytes.end(), m_image.begin() + offset);
    m_senseKey = senseNoSense;
    return true;
}

ScsiCommandResult ScsiBlockDevice::good(QByteArray data) const
{
    return { std::move(data), statusGood, messageCommandComplete, senseNoSense };
}

ScsiCommandResult ScsiBlockDevice::checkCondition(std::uint8_t senseKey)
{
    m_senseKey = senseKey;
    return { {}, statusCheckCondition, messageCommandComplete, senseKey };
}

ScsiCommandResult ScsiBlockDevice::inquiry(bool evpd, std::uint8_t pageCode, std::uint8_t allocationLength)
{
    if (evpd) {
        QByteArray data;
        switch (pageCode) {
        case 0x00:
            data.append(static_cast<char>(0x00));
            data.append(static_cast<char>(0x00));
            data.append(static_cast<char>(0x00));
            data.append(static_cast<char>(0x05));
            data.append(static_cast<char>(0x00));
            data.append(static_cast<char>(0x80));
            data.append(static_cast<char>(0x81));
            data.append(static_cast<char>(0x82));
            data.append(static_cast<char>(0x83));
            return good(clipped(data, allocationLength));
        case 0x80:
            data.append(static_cast<char>(0x00));
            data.append(static_cast<char>(0x80));
            data.append(static_cast<char>(0x00));
            data.append(static_cast<char>(0x10));
            data.append(QByteArrayLiteral("CUTEMAC000000001"));
            return good(clipped(data, allocationLength));
        case 0x81:
            data.append(static_cast<char>(0x00));
            data.append(static_cast<char>(0x81));
            data.append(static_cast<char>(0x00));
            data.append(static_cast<char>(0x03));
            data.append(static_cast<char>(0x03));
            data.append(static_cast<char>(0x03));
            data.append(static_cast<char>(0x03));
            return good(clipped(data, allocationLength));
        case 0x82:
            data.append(static_cast<char>(0x00));
            data.append(static_cast<char>(0x82));
            data.append(static_cast<char>(0x00));
            data.append(static_cast<char>(0x07));
            data.append(static_cast<char>(0x06));
            data.append(QByteArrayLiteral("SCSI-2"));
            return good(clipped(data, allocationLength));
        case 0x83: {
            const QByteArray identifier = QByteArrayLiteral("CUTEMAC:SCSI:DISK:00000001");
            data.append(static_cast<char>(0x00));
            data.append(static_cast<char>(0x83));
            data.append(static_cast<char>(0x00));
            data.append(static_cast<char>(identifier.size() + 4));
            data.append(static_cast<char>(0x02)); // ASCII identifier.
            data.append(static_cast<char>(0x01)); // T10 vendor ID association.
            data.append(static_cast<char>(0x00));
            data.append(static_cast<char>(identifier.size()));
            data.append(identifier);
            return good(clipped(data, allocationLength));
        }
        default:
            return checkCondition(senseIllegalRequest);
        }
    }

    if (pageCode != 0) {
        return checkCondition(senseIllegalRequest);
    }

    QByteArray data(36, '\0');
    data[0] = 0x00; // direct-access block device
    data[1] = 0x00;
    data[2] = 0x02; // SCSI-2, matching modern Apple-compatible SCSI bridges.
    data[3] = 0x01;
    data[4] = 31;
    data[7] = 0x18; // linked commands and synchronous transfer supported.
    data.replace(8, 8, QByteArrayLiteral(" SEAGATE"));
    data.replace(16, 16, QByteArrayLiteral("          ST225N"));
    data.replace(32, 4, QByteArrayLiteral("1.0 "));
    return good(clipped(data, allocationLength));
}

ScsiCommandResult ScsiBlockDevice::requestSense(std::uint8_t allocationLength) const
{
    QByteArray data(18, '\0');
    data[0] = 0x70;
    data[2] = m_senseKey;
    data[7] = 10;
    data.truncate(std::min<int>(allocationLength, data.size()));
    return good(data);
}

ScsiCommandResult ScsiBlockDevice::modeSense(std::uint8_t pageCode, std::uint8_t allocationLength)
{
    QByteArray data(4, '\0');
    data[2] = m_readOnly ? static_cast<char>(0x80) : static_cast<char>(0x00);
    if (pageCode == 0x30 || pageCode == 0x3f) {
        data.append(static_cast<char>(0x30));
        data.append(static_cast<char>(0x24));
        QByteArray page(0x24, '\0');
        page.replace(8, 22, QByteArrayLiteral("APPLE COMPUTER, INC   "));
        data.append(page);
    } else if (pageCode != 0x00) {
        return checkCondition(senseIllegalRequest);
    }

    data[0] = static_cast<char>(data.size() - 1);
    data[2] = m_readOnly ? 0x80 : 0x00;
    return good(clipped(data, allocationLength));
}

ScsiCommandResult ScsiBlockDevice::readCapacity() const
{
    QByteArray data;
    appendBe32(data, blockCount() == 0 ? 0 : blockCount() - 1);
    appendBe32(data, blockSize);
    return good(data);
}

} // namespace cutemac::devices::scsi
