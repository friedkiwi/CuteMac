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

ScsiCommandResult ScsiCdRomDevice::executeCommand(const QByteArray& cdb, const QByteArray&)
{
    if (cdb.isEmpty()) return checkCondition(senseIllegalRequest, 0x24);
    const auto opcode = static_cast<std::uint8_t>(cdb[0]);
    if (m_unitAttention && opcode != 0x03 && opcode != 0x12) {
        m_unitAttention = false;
        return checkCondition(senseUnitAttention, ascMediumChanged);
    }
    if (!ready() && opcode != 0x03 && opcode != 0x12) return checkCondition(senseNotReady, ascMediumNotPresent);

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
    case 0x1a: return modeSense(false, cdb.size() > 2 ? static_cast<std::uint8_t>(cdb[2]) & 0x3f : 0x3f,
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
    case 0x5a:
        if (cdb.size() < 10) return checkCondition(senseIllegalRequest, 0x24);
        return modeSense(true, static_cast<std::uint8_t>(cdb[2]) & 0x3f,
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
    if (lba > blockCount() || blocks > blockCount() - lba) return checkCondition(senseIllegalRequest, 0x21);
    m_senseKey = senseNoSense;
    m_additionalSenseCode = 0;
    return good(m_image.mid(static_cast<qsizetype>(lba) * blockSize, static_cast<qsizetype>(blocks) * blockSize));
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

ScsiCommandResult ScsiCdRomDevice::modeSense(bool tenByte, std::uint8_t pageCode, std::uint16_t allocationLength) const
{
    QByteArray data(tenByte ? 8 : 4, '\0');
    // The device-specific parameter byte is byte 2 in a MODE SENSE(6)
    // header and byte 3 in a MODE SENSE(10) header.  Bit 7 advertises
    // write protection; classic Mac OS uses it to avoid desktop-file writes.
    data[tenByte ? 3 : 2] = static_cast<char>(0x80);
    if (pageCode == 0x0d || pageCode == 0x3f) data.append(QByteArray::fromHex("0d06000d003c004b"));
    if (pageCode == 0x2a || pageCode == 0x3f) data.append(QByteArray::fromHex("2a0e0000000328000562000000400562"));
    if (data.size() == (tenByte ? 8 : 4)) return { {}, 0x02, 0, senseIllegalRequest };
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
    appendBe32(data, blockCount() == 0 ? 0 : blockCount() - 1);
    appendBe32(data, blockSize);
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
