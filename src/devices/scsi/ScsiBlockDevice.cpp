#include "cutemac/devices/scsi/ScsiBlockDevice.h"

#include <QFile>

#include <algorithm>
#include <optional>

namespace cutemac::devices::scsi {

namespace {

constexpr std::uint32_t blockSize = 512;
constexpr std::uint8_t statusGood = 0x00;
constexpr std::uint8_t statusCheckCondition = 0x02;
constexpr std::uint8_t messageCommandComplete = 0x00;
constexpr std::uint8_t senseNoSense = 0x00;
constexpr std::uint8_t senseNotReady = 0x02;
constexpr std::uint8_t senseIllegalRequest = 0x05;
constexpr std::uint64_t MiB = 1024ULL * 1024ULL;

QByteArray padded(const char* text, qsizetype width)
{
    auto bytes = QByteArray(text);
    bytes.truncate(width);
    if (bytes.size() < width) bytes.append(QByteArray(width - bytes.size(), ' '));
    return bytes;
}

void appendBe32(QByteArray& bytes, std::uint32_t value)
{
    bytes.append(static_cast<char>(value >> 24));
    bytes.append(static_cast<char>(value >> 16));
    bytes.append(static_cast<char>(value >> 8));
    bytes.append(static_cast<char>(value));
}

std::uint32_t readBe32(const QByteArray& bytes, qsizetype offset)
{
    return (static_cast<std::uint32_t>(static_cast<std::uint8_t>(bytes[offset])) << 24)
        | (static_cast<std::uint32_t>(static_cast<std::uint8_t>(bytes[offset + 1])) << 16)
        | (static_cast<std::uint32_t>(static_cast<std::uint8_t>(bytes[offset + 2])) << 8)
        | static_cast<std::uint8_t>(bytes[offset + 3]);
}

QByteArray clipped(QByteArray bytes, std::uint8_t allocationLength)
{
    bytes.truncate(std::min<int>(allocationLength, bytes.size()));
    return bytes;
}

} // namespace

ScsiDiskIdentity ScsiBlockDevice::identityForSize(std::uint64_t sizeBytes)
{
    const auto identity = [sizeBytes](std::uint64_t mib, const char* vendor, const char* product) -> std::optional<ScsiDiskIdentity> {
        if (sizeBytes != mib * MiB) return std::nullopt;
        return ScsiDiskIdentity { padded(vendor, 8), padded(product, 16), padded("1.0", 4) };
    };
    if (auto value = identity(20, "CONNER", "CP2025-20mb")) return *value;
    if (auto value = identity(40, "QUANTUM", "GO40S")) return *value;
    if (auto value = identity(80, "QUANTUM", "GO80S1")) return *value;
    if (auto value = identity(160, "QUANTUM", "GO160S")) return *value;
    if (auto value = identity(230, "QUANTUM", "LP240S")) return *value;
    if (auto value = identity(500, "QUANTUM", "LPS540S")) return *value;
    if (auto value = identity(1024, "IBM", "DPES-31080")) return *value;
    return { padded("QUANTUM", 8), padded("FIREBALL1", 16), padded("1.0", 4) };
}

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
    case 0x01:
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
    case 0x0b:
        return good();
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
            cdb.size() > 1 && (static_cast<std::uint8_t>(cdb[1]) & 0x08) != 0,
            cdb.size() > 2 ? static_cast<std::uint8_t>(cdb[2]) & 0x3f : 0x3f,
            cdb.size() > 4 ? static_cast<std::uint8_t>(cdb[4]) : 4);
    case 0x25:
        return readCapacity();
    case 0x28: {
        if (cdb.size() < 10) {
            return checkCondition(senseIllegalRequest);
        }
        const auto lba = readBe32(cdb, 2);
        const auto blocks = (static_cast<std::uint32_t>(static_cast<std::uint8_t>(cdb[7])) << 8)
            | static_cast<std::uint8_t>(cdb[8]);
        if (blocks == 0) {
            return good();
        }
        const auto data = readBlocks(lba, blocks);
        return m_senseKey == senseNoSense ? good(data) : checkCondition(m_senseKey);
    }
    case 0x2a: {
        if (cdb.size() < 10 || m_readOnly) {
            return checkCondition(senseIllegalRequest);
        }
        const auto lba = readBe32(cdb, 2);
        const auto blocks = (static_cast<std::uint32_t>(static_cast<std::uint8_t>(cdb[7])) << 8)
            | static_cast<std::uint8_t>(cdb[8]);
        if (dataOut.size() != static_cast<qsizetype>(blocks) * blockSize) {
            return checkCondition(senseIllegalRequest);
        }
        return blocks == 0 || writeBlocks(lba, dataOut) ? good() : checkCondition(senseIllegalRequest);
    }
    case 0x2b:
    case 0x35:
        return good();
    case 0x2f: {
        if (cdb.size() < 10 || (static_cast<std::uint8_t>(cdb[1]) & 0x02) != 0) {
            return checkCondition(senseIllegalRequest);
        }
        const auto lba = (static_cast<std::uint32_t>(static_cast<std::uint8_t>(cdb[2])) << 24)
            | (static_cast<std::uint32_t>(static_cast<std::uint8_t>(cdb[3])) << 16)
            | (static_cast<std::uint32_t>(static_cast<std::uint8_t>(cdb[4])) << 8)
            | static_cast<std::uint8_t>(cdb[5]);
        const auto blocks = (static_cast<std::uint32_t>(static_cast<std::uint8_t>(cdb[7])) << 8)
            | static_cast<std::uint8_t>(cdb[8]);
        return lba <= blockCount() && blocks <= blockCount() - lba ? good() : checkCondition(senseIllegalRequest);
    }
    case 0x37: {
        if (cdb.size() < 10) return checkCondition(senseIllegalRequest);
        const auto allocationLength = (static_cast<int>(static_cast<std::uint8_t>(cdb[7])) << 8)
            | static_cast<std::uint8_t>(cdb[8]);
        QByteArray data(4, '\0');
        data[1] = cdb[2];
        data.truncate(std::min<qsizetype>(allocationLength, data.size()));
        return good(data);
    }
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
    const auto identity = identityForSize(static_cast<std::uint64_t>(m_image.size()));
    data.replace(8, 8, identity.vendor);
    data.replace(16, 16, identity.product);
    data.replace(32, 4, identity.revision);
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

ScsiCommandResult ScsiBlockDevice::modeSense(bool disableBlockDescriptors,
    std::uint8_t pageCode, std::uint8_t allocationLength)
{
    QByteArray data(4, '\0');
    data[2] = m_readOnly ? static_cast<char>(0x80) : static_cast<char>(0x00);
    bool pageFound = pageCode == 0x00;
    // Disk-format and geometry consumers, including Apple's setup tools,
    // expect the direct-access block descriptor when DBD is clear.  Without
    // it they interpret the first mode-page bytes as capacity and block size,
    // shifting the complete page layout by eight bytes.
    if (!disableBlockDescriptors
        && (pageCode == 0x03 || pageCode == 0x04 || pageCode == 0x30 || pageCode == 0x3f)) {
        QByteArray descriptor(8, '\0');
        const auto blocks = std::min<std::uint32_t>(blockCount(), 0x00ffffffU);
        descriptor[1] = static_cast<char>(blocks >> 16);
        descriptor[2] = static_cast<char>(blocks >> 8);
        descriptor[3] = static_cast<char>(blocks);
        descriptor[5] = static_cast<char>(blockSize >> 16);
        descriptor[6] = static_cast<char>(blockSize >> 8);
        descriptor[7] = static_cast<char>(blockSize);
        data.append(descriptor);
        data[3] = 8;
    }
    if (pageCode == 0x03 || pageCode == 0x3f) {
        pageFound = true;
        QByteArray page(24, '\0');
        page[0] = static_cast<char>(0x83);
        page[1] = 0x16;
        page[10] = 0x00;
        page[11] = 0x20; // 32 sectors per track.
        page[12] = 0x02;
        page[13] = 0x00;
        page[15] = 0x01;
        page[20] = static_cast<char>(0xc0);
        data.append(page);
    }
    if (pageCode == 0x04 || pageCode == 0x3f) {
        pageFound = true;
        constexpr std::uint32_t heads = 16;
        constexpr std::uint32_t sectorsPerTrack = 32;
        const auto cylinders = std::max<std::uint32_t>(1, (blockCount() + heads * sectorsPerTrack - 1) / (heads * sectorsPerTrack));
        QByteArray page(24, '\0');
        page[0] = 0x04;
        page[1] = 0x16;
        page[2] = static_cast<char>(cylinders >> 16);
        page[3] = static_cast<char>(cylinders >> 8);
        page[4] = static_cast<char>(cylinders);
        page[5] = static_cast<char>(heads);
        page[6] = page[2];
        page[7] = page[3];
        page[8] = page[4];
        page[9] = page[2];
        page[10] = page[3];
        page[11] = page[4];
        page[13] = 0x01;
        page[20] = static_cast<char>(5400 >> 8);
        page[21] = static_cast<char>(5400);
        data.append(page);
    }
    if (pageCode == 0x30 || pageCode == 0x3f) {
        pageFound = true;
        data.append(static_cast<char>(0x30));
        data.append(static_cast<char>(0x16));
        data.append(QByteArrayLiteral("APPLE COMPUTER, INC   "));
    }
    if (!pageFound) {
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
