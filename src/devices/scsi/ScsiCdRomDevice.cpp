#include "cutemac/devices/scsi/ScsiCdRomDevice.h"

#include <algorithm>

#include <QFile>

namespace cutemac::devices::scsi {

namespace {
constexpr std::uint32_t blockSize = 2048;
constexpr std::uint8_t senseNoSense = 0x00;
constexpr std::uint8_t senseNotReady = 0x02;
constexpr std::uint8_t senseIllegalRequest = 0x05;
constexpr std::uint8_t senseUnitAttention = 0x06;
constexpr std::uint8_t ascMediumChanged = 0x28;
constexpr std::uint8_t ascMediumNotPresent = 0x3a;

void appendBe16(QByteArray& data, std::uint16_t value)
{
    data.append(static_cast<char>(value >> 8));
    data.append(static_cast<char>(value));
}

void appendBe32(QByteArray& data, std::uint32_t value)
{
    data.append(static_cast<char>(value >> 24));
    data.append(static_cast<char>(value >> 16));
    data.append(static_cast<char>(value >> 8));
    data.append(static_cast<char>(value));
}

std::uint32_t be32(const QByteArray& data, int offset)
{
    return (static_cast<std::uint32_t>(static_cast<std::uint8_t>(data[offset])) << 24)
        | (static_cast<std::uint32_t>(static_cast<std::uint8_t>(data[offset + 1])) << 16)
        | (static_cast<std::uint32_t>(static_cast<std::uint8_t>(data[offset + 2])) << 8)
        | static_cast<std::uint8_t>(data[offset + 3]);
}

void appendAddress(QByteArray& data, std::uint32_t lba, bool msf)
{
    if (!msf) {
        appendBe32(data, lba);
        return;
    }
    const auto frames = lba + 150;
    data.append('\0');
    data.append(static_cast<char>(frames / (60 * 75)));
    data.append(static_cast<char>((frames / 75) % 60));
    data.append(static_cast<char>(frames % 75));
}
} // namespace

bool ScsiCdRomDevice::loadImage(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return false;
    auto image = file.readAll();
    if (image.isEmpty() || (image.size() % blockSize) != 0) return false;
    m_image = std::move(image);
    m_imagePath = path;
    m_senseKey = senseUnitAttention;
    m_additionalSenseCode = ascMediumChanged;
    m_unitAttention = true;
    return true;
}

void ScsiCdRomDevice::eject()
{
    m_image.clear();
    m_imagePath.clear();
    m_senseKey = senseUnitAttention;
    m_additionalSenseCode = ascMediumChanged;
    m_unitAttention = true;
}

void ScsiCdRomDevice::acknowledgeMediaChange()
{
    m_unitAttention = false;
    m_senseKey = ready() ? senseNoSense : senseNotReady;
    m_additionalSenseCode = ready() ? 0 : ascMediumNotPresent;
}

bool ScsiCdRomDevice::ready() const { return !m_image.isEmpty(); }

ScsiCommandResult ScsiCdRomDevice::executeCommand(const QByteArray& cdb, const QByteArray& dataOut)
{
    if (cdb.isEmpty()) return checkCondition(senseIllegalRequest, 0x24);
    const auto opcode = static_cast<std::uint8_t>(cdb[0]);
    if (m_unitAttention && opcode != 0x03 && opcode != 0x12) {
        m_unitAttention = false;
        return checkCondition(senseUnitAttention, ascMediumChanged);
    }
    // An empty removable drive remains a usable SCSI logical unit. Device-level
    // commands must still work so the Macintosh CD-ROM driver can finish opening
    // the drive and install its SystemTask media poll. Only commands which
    // actually require medium should report NOT READY / MEDIUM NOT PRESENT.
    const bool allowedWithoutMedium = opcode == 0x03 // REQUEST SENSE
        || opcode == 0x12                           // INQUIRY
        || opcode == 0x15                           // MODE SELECT(6)
        || opcode == 0x1a                           // MODE SENSE(6)
        || opcode == 0x1b                           // START STOP UNIT
        || opcode == 0x1e                           // PREVENT/ALLOW MEDIUM REMOVAL
        || opcode == 0x55                           // MODE SELECT(10)
        || opcode == 0x5a;                          // MODE SENSE(10)
    if (!ready() && !allowedWithoutMedium) return checkCondition(senseNotReady, ascMediumNotPresent);

    switch (opcode) {
    case 0x00: return good();
    case 0x03: return requestSense(cdb.size() > 4 ? static_cast<std::uint8_t>(cdb[4]) : 18);
    case 0x08: {
        if (cdb.size() < 6) return checkCondition(senseIllegalRequest, 0x24);
        const auto lba = (static_cast<std::uint32_t>(static_cast<std::uint8_t>(cdb[1]) & 0x1f) << 16)
            | (static_cast<std::uint32_t>(static_cast<std::uint8_t>(cdb[2])) << 8)
            | static_cast<std::uint8_t>(cdb[3]);
        const auto blocks = static_cast<std::uint8_t>(cdb[4]) == 0 ? 256U : static_cast<std::uint32_t>(static_cast<std::uint8_t>(cdb[4]));
        return read(lba, blocks);
    }
    case 0x12: return inquiry(cdb.size() > 4 ? static_cast<std::uint8_t>(cdb[4]) : 36);
    case 0x15: return modeSelect(false, dataOut);
    case 0x1a: return modeSense(false, cdb.size() > 1 && (static_cast<std::uint8_t>(cdb[1]) & 0x08) != 0,
        cdb.size() > 2 ? static_cast<std::uint8_t>(cdb[2]) & 0x3f : 0x3f,
        cdb.size() > 4 ? static_cast<std::uint8_t>(cdb[4]) : 4);
    case 0x1b:
        if (cdb.size() > 4 && (static_cast<std::uint8_t>(cdb[4]) & 0x02) != 0
            && (static_cast<std::uint8_t>(cdb[4]) & 0x01) == 0) eject();
        return good();
    case 0x1e: return good();
    case 0x25: return readCapacity();
    case 0x28:
        if (cdb.size() < 10) return checkCondition(senseIllegalRequest, 0x24);
        return read(be32(cdb, 2), (static_cast<std::uint32_t>(static_cast<std::uint8_t>(cdb[7])) << 8) | static_cast<std::uint8_t>(cdb[8]));
    case 0x43: return readToc(cdb);
    case 0x55: return modeSelect(true, dataOut);
    case 0x5a:
        if (cdb.size() < 10) return checkCondition(senseIllegalRequest, 0x24);
        return modeSense(true, (static_cast<std::uint8_t>(cdb[1]) & 0x08) != 0,
            static_cast<std::uint8_t>(cdb[2]) & 0x3f,
            (static_cast<std::uint16_t>(static_cast<std::uint8_t>(cdb[7])) << 8) | static_cast<std::uint8_t>(cdb[8]));
    case 0xa8:
        if (cdb.size() < 12) return checkCondition(senseIllegalRequest, 0x24);
        return read(be32(cdb, 2), be32(cdb, 6));
    case 0xd8:
        if (cdb.size() < 12 || static_cast<std::uint8_t>(cdb[10]) != 0) return checkCondition(senseIllegalRequest, 0x24);
        return readAppleRaw(be32(cdb, 2), be32(cdb, 6));
    case 0xd9: {
        if (cdb.size() < 12 || static_cast<std::uint8_t>(cdb[10]) != 0) return checkCondition(senseIllegalRequest, 0x24);
        const auto startFrames = (static_cast<std::uint32_t>(static_cast<std::uint8_t>(cdb[3])) * 60
                                     + static_cast<std::uint8_t>(cdb[4])) * 75
            + static_cast<std::uint8_t>(cdb[5]);
        const auto endFrames = (static_cast<std::uint32_t>(static_cast<std::uint8_t>(cdb[7])) * 60
                                   + static_cast<std::uint8_t>(cdb[8])) * 75
            + static_cast<std::uint8_t>(cdb[9]);
        if (startFrames < 150 || endFrames < startFrames) return checkCondition(senseIllegalRequest, 0x24);
        return readAppleRaw(startFrames - 150, endFrames - startFrames);
    }
    default: return checkCondition(senseIllegalRequest, 0x20);
    }
}

ScsiCommandResult ScsiCdRomDevice::readAppleRaw(std::uint32_t lba, std::uint32_t blocks)
{
    if (lba > blockCount() || blocks > blockCount() - lba) return checkCondition(senseIllegalRequest, 0x21);
    QByteArray raw;
    raw.reserve(static_cast<qsizetype>(blocks) * 2352);
    for (std::uint32_t block = 0; block < blocks; ++block) {
        QByteArray sector(2352, '\0');
        sector[0] = 0;
        std::fill(sector.begin() + 1, sector.begin() + 11, static_cast<char>(0xff));
        sector[11] = 0;
        const auto frames = lba + block + 150;
        sector[12] = static_cast<char>(frames / (60 * 75));
        sector[13] = static_cast<char>((frames / 75) % 60);
        sector[14] = static_cast<char>(frames % 75);
        sector[15] = 1;
        std::copy_n(m_image.cbegin() + static_cast<qsizetype>(lba + block) * blockSize, blockSize, sector.begin() + 16);
        raw.append(sector);
    }
    return good(raw);
}

std::uint32_t ScsiCdRomDevice::blockCount() const { return static_cast<std::uint32_t>(m_image.size() / blockSize); }

ScsiCommandResult ScsiCdRomDevice::read(std::uint32_t lba, std::uint32_t blocks)
{
    const auto logicalBlocks = static_cast<std::uint64_t>(m_image.size()) / m_logicalBlockSize;
    if (lba > logicalBlocks || blocks > logicalBlocks - lba) return checkCondition(senseIllegalRequest, 0x21);
    m_senseKey = senseNoSense;
    m_additionalSenseCode = 0;
    return good(m_image.mid(static_cast<qsizetype>(lba) * m_logicalBlockSize,
        static_cast<qsizetype>(blocks) * m_logicalBlockSize));
}

ScsiCommandResult ScsiCdRomDevice::modeSelect(bool tenByte, const QByteArray& parameters)
{
    const int headerSize = tenByte ? 8 : 4;
    if (parameters.size() < headerSize) return checkCondition(senseIllegalRequest, 0x26);
    const auto descriptorLength = tenByte
        ? (static_cast<std::uint16_t>(static_cast<std::uint8_t>(parameters[6])) << 8)
            | static_cast<std::uint8_t>(parameters[7])
        : static_cast<std::uint8_t>(parameters[3]);
    if (descriptorLength == 0) return good();
    if (descriptorLength < 8 || parameters.size() < headerSize + descriptorLength)
        return checkCondition(senseIllegalRequest, 0x26);
    const auto descriptor = headerSize;
    const auto requestedBlockSize = (static_cast<std::uint32_t>(static_cast<std::uint8_t>(parameters[descriptor + 5])) << 16)
        | (static_cast<std::uint32_t>(static_cast<std::uint8_t>(parameters[descriptor + 6])) << 8)
        | static_cast<std::uint8_t>(parameters[descriptor + 7]);
    if (requestedBlockSize != 512 && requestedBlockSize != 1024 && requestedBlockSize != blockSize)
        return checkCondition(senseIllegalRequest, 0x26);
    m_logicalBlockSize = requestedBlockSize;
    return good();
}

ScsiCommandResult ScsiCdRomDevice::inquiry(std::uint8_t allocationLength) const
{
    QByteArray data(36, '\0');
    data[0] = 0x05;
    data[1] = static_cast<char>(0x80);
    data[2] = 0x02;
    data[3] = 0x01;
    data[4] = 31;
    data.replace(8, 8, QByteArrayLiteral("MATSHITA"));
    data.replace(16, 16, QByteArrayLiteral("CD-ROM CR-8004  "));
    data.replace(32, 4, QByteArrayLiteral("1.1f"));
    data.truncate(std::min<int>(allocationLength, data.size()));
    return good(data);
}

ScsiCommandResult ScsiCdRomDevice::requestSense(std::uint8_t allocationLength)
{
    QByteArray data(18, '\0');
    data[0] = 0x70;
    data[2] = static_cast<char>(m_senseKey);
    data[7] = 10;
    data[12] = static_cast<char>(m_additionalSenseCode);
    data.truncate(std::min<int>(allocationLength, data.size()));
    m_unitAttention = false;
    m_senseKey = ready() ? senseNoSense : senseNotReady;
    m_additionalSenseCode = ready() ? 0 : ascMediumNotPresent;
    return good(data);
}

ScsiCommandResult ScsiCdRomDevice::modeSense(bool tenByte, bool disableBlockDescriptors,
    std::uint8_t pageCode, std::uint16_t allocationLength) const
{
    QByteArray data(tenByte ? 8 : 4, '\0');
    // The device-specific parameter byte is byte 2 in a MODE SENSE(6)
    // header and byte 3 in a MODE SENSE(10) header.  Bit 7 advertises
    // write protection; classic Mac OS uses it to avoid desktop-file writes.
    data[tenByte ? 3 : 2] = static_cast<char>(0x80);
    if (!disableBlockDescriptors) {
        QByteArray descriptor(8, '\0');
        const auto blocks = std::min<std::uint64_t>(
            static_cast<std::uint64_t>(m_image.size()) / m_logicalBlockSize, 0x00ffffffU);
        descriptor[1] = static_cast<char>(blocks >> 16);
        descriptor[2] = static_cast<char>(blocks >> 8);
        descriptor[3] = static_cast<char>(blocks);
        descriptor[5] = static_cast<char>(m_logicalBlockSize >> 16);
        descriptor[6] = static_cast<char>(m_logicalBlockSize >> 8);
        descriptor[7] = static_cast<char>(m_logicalBlockSize);
        data.append(descriptor);
        data[tenByte ? 7 : 3] = 8;
    }
    // Read/write error recovery. Apple CD-ROM software, including the A/UX 3
    // startup extension, explicitly probes this mandatory direct-access page.
    // Report the conservative defaults used by early SCSI CD-ROM drives.
    if (pageCode == 0x01 || pageCode == 0x3f) data.append(QByteArray::fromHex("0106000000000000"));
    if (pageCode == 0x0d || pageCode == 0x3f) data.append(QByteArray::fromHex("0d06000d003c004b"));
    if (pageCode == 0x2a || pageCode == 0x3f) data.append(QByteArray::fromHex("2a0e0000000328000562000000400562"));
    if (pageCode != 0x00 && pageCode != 0x01 && pageCode != 0x0d && pageCode != 0x2a && pageCode != 0x3f)
        return { {}, 0x02, 0, senseIllegalRequest };
    if (tenByte) {
        const auto length = static_cast<std::uint16_t>(data.size() - 2);
        data[0] = static_cast<char>(length >> 8);
        data[1] = static_cast<char>(length);
    } else data[0] = static_cast<char>(data.size() - 1);
    data.truncate(std::min<int>(allocationLength, data.size()));
    return good(data);
}

ScsiCommandResult ScsiCdRomDevice::readCapacity() const
{
    QByteArray data;
    const auto blocks = static_cast<std::uint32_t>(m_image.size() / m_logicalBlockSize);
    appendBe32(data, blocks == 0 ? 0 : blocks - 1);
    appendBe32(data, m_logicalBlockSize);
    return good(data);
}

ScsiCommandResult ScsiCdRomDevice::readToc(const QByteArray& cdb) const
{
    if (cdb.size() < 10) return { {}, 0x02, 0, senseIllegalRequest };
    const bool msf = (static_cast<std::uint8_t>(cdb[1]) & 0x02) != 0;
    const auto firstRequested = static_cast<std::uint8_t>(cdb[6]);
    const auto allocationLength = (static_cast<std::uint16_t>(static_cast<std::uint8_t>(cdb[7])) << 8) | static_cast<std::uint8_t>(cdb[8]);
    if (firstRequested != 0 && firstRequested != 1 && firstRequested != 0xaa) return { {}, 0x02, 0, senseIllegalRequest };
    QByteArray descriptors;
    if (firstRequested != 0xaa) {
        descriptors.append('\0'); descriptors.append(static_cast<char>(0x14)); descriptors.append(1); descriptors.append('\0');
        appendAddress(descriptors, 0, msf);
    }
    descriptors.append('\0'); descriptors.append(static_cast<char>(0x14)); descriptors.append(static_cast<char>(0xaa)); descriptors.append('\0');
    appendAddress(descriptors, blockCount(), msf);
    QByteArray data;
    appendBe16(data, static_cast<std::uint16_t>(descriptors.size() + 2));
    data.append(1); data.append(1); data.append(descriptors);
    data.truncate(std::min<int>(allocationLength, data.size()));
    return good(data);
}

ScsiCommandResult ScsiCdRomDevice::good(QByteArray data) const { return { std::move(data), 0, 0, senseNoSense }; }

ScsiCommandResult ScsiCdRomDevice::checkCondition(std::uint8_t senseKey, std::uint8_t asc)
{
    m_senseKey = senseKey;
    m_additionalSenseCode = asc;
    return { {}, 0x02, 0, senseKey };
}

} // namespace cutemac::devices::scsi
