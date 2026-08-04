#include "cutemac/devices/floppy/FloppyDiskImage.h"

#include <QFile>
#include <QFileInfo>
#include <QSaveFile>

#include <algorithm>
#include <array>

namespace cutemac::devices::floppy {

namespace {

constexpr int bytesPerSector = 512;
constexpr int tagBytesPerSector = 12;
constexpr int encodedPayloadBytes = bytesPerSector + tagBytesPerSector;
constexpr int tracksPerSide = 80;
constexpr int raw400KBytes = 400 * 1024;
constexpr int raw800KBytes = 800 * 1024;
constexpr int raw1440KBytes = 1440 * 1024;
constexpr int hfsGeometrySlackBytes = 4 * 1024;
constexpr qsizetype maxTraceEvents = 4096;
constexpr qsizetype maxLastNibbles = 4096;

constexpr std::array<std::uint8_t, 64> gcrEncodeTable {
    0x96, 0x97, 0x9a, 0x9b, 0x9d, 0x9e, 0x9f, 0xa6,
    0xa7, 0xab, 0xac, 0xad, 0xae, 0xaf, 0xb2, 0xb3,
    0xb4, 0xb5, 0xb6, 0xb7, 0xb9, 0xba, 0xbb, 0xbc,
    0xbd, 0xbe, 0xbf, 0xcb, 0xcd, 0xce, 0xcf, 0xd3,
    0xd6, 0xd7, 0xd9, 0xda, 0xdb, 0xdc, 0xdd, 0xde,
    0xdf, 0xe5, 0xe6, 0xe7, 0xe9, 0xea, 0xeb, 0xec,
    0xed, 0xee, 0xef, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6,
    0xf7, 0xf9, 0xfa, 0xfb, 0xfc, 0xfd, 0xfe, 0xff,
};

[[nodiscard]] std::uint32_t readBe32(const QByteArray& bytes, qsizetype offset)
{
    if (offset + 4 > bytes.size()) {
        return 0;
    }
    return (static_cast<std::uint32_t>(static_cast<std::uint8_t>(bytes[offset])) << 24)
        | (static_cast<std::uint32_t>(static_cast<std::uint8_t>(bytes[offset + 1])) << 16)
        | (static_cast<std::uint32_t>(static_cast<std::uint8_t>(bytes[offset + 2])) << 8)
        | static_cast<std::uint8_t>(bytes[offset + 3]);
}

void writeBe32(QByteArray& bytes, qsizetype offset, std::uint32_t value)
{
    if (offset + 4 > bytes.size()) return;
    bytes[offset] = static_cast<char>(value >> 24);
    bytes[offset + 1] = static_cast<char>(value >> 16);
    bytes[offset + 2] = static_cast<char>(value >> 8);
    bytes[offset + 3] = static_cast<char>(value);
}

[[nodiscard]] int decodeGcr(std::uint8_t value)
{
    const auto found = std::find(gcrEncodeTable.begin(), gcrEncodeTable.end(), value);
    return found == gcrEncodeTable.end() ? -1 : static_cast<int>(found - gcrEncodeTable.begin());
}

[[nodiscard]] std::uint32_t diskCopyChecksum(const QByteArray& bytes)
{
    std::uint32_t checksum = 0;
    for (qsizetype offset = 0; offset + 1 < bytes.size(); offset += 2) {
        checksum += (static_cast<std::uint32_t>(static_cast<std::uint8_t>(bytes[offset])) << 8)
            | static_cast<std::uint8_t>(bytes[offset + 1]);
        checksum = (checksum >> 1) | (checksum << 31);
    }
    return checksum;
}

[[nodiscard]] FloppyDiskImage::Kind truncatedRawHfsKind(const QByteArray& bytes)
{
    if (bytes.size() <= raw400KBytes || bytes.size() >= raw1440KBytes || bytes.size() < 1056) {
        return FloppyDiskImage::Kind::Empty;
    }
    const auto targetBytes = bytes.size() < raw800KBytes ? raw800KBytes : raw1440KBytes;
    const auto mdb = bytes.mid(1024, 32);
    const auto be16 = [&](qsizetype offset) {
        return (static_cast<std::uint16_t>(static_cast<std::uint8_t>(mdb[offset])) << 8)
            | static_cast<std::uint8_t>(mdb[offset + 1]);
    };
    const auto allocationBlocks = be16(18);
    const auto allocationBlockSize = (static_cast<std::uint32_t>(be16(20)) << 16) | be16(22);
    const auto firstAllocationBlock = be16(28);
    const auto volumeBytes = (static_cast<std::uint64_t>(firstAllocationBlock) * bytesPerSector)
        + static_cast<std::uint64_t>(allocationBlocks) * allocationBlockSize;
    if (be16(0) != 0x4244 || allocationBlocks == 0 || allocationBlockSize == 0
        || volumeBytes <= static_cast<std::uint64_t>(bytes.size())
        || volumeBytes > static_cast<std::uint64_t>(targetBytes + hfsGeometrySlackBytes)) {
        return FloppyDiskImage::Kind::Empty;
    }
    return targetBytes == raw800KBytes ? FloppyDiskImage::Kind::Raw800K : FloppyDiskImage::Kind::Raw1440K;
}

[[nodiscard]] bool decodeGcrPayload(const QByteArray& encoded, QByteArray& payload)
{
    if (encoded.size() != 703) return false;
    payload = QByteArray(encodedPayloadBytes, '\0');
    std::uint8_t checksumA = 0, checksumB = 0, checksumC = 0;
    qsizetype input = 0;
    for (int group = 0; group < 175; ++group) {
        const auto packed = decodeGcr(static_cast<std::uint8_t>(encoded[input++]));
        const auto lowA = decodeGcr(static_cast<std::uint8_t>(encoded[input++]));
        const auto lowB = decodeGcr(static_cast<std::uint8_t>(encoded[input++]));
        const auto lowC = group != 174 ? decodeGcr(static_cast<std::uint8_t>(encoded[input++])) : 0;
        if (packed < 0 || lowA < 0 || lowB < 0 || lowC < 0) return false;

        const auto encodedA = static_cast<std::uint8_t>(lowA | ((packed & 0x30) << 2));
        const auto encodedB = static_cast<std::uint8_t>(lowB | ((packed & 0x0c) << 4));
        const auto encodedC = static_cast<std::uint8_t>(lowC | ((packed & 0x03) << 6));
        checksumC = static_cast<std::uint8_t>((checksumC << 1) | (checksumC >> 7));
        const auto plainA = static_cast<std::uint8_t>(encodedA ^ checksumC);
        const auto sumA = static_cast<unsigned>(checksumA) + plainA + (checksumC & 1U);
        checksumA = static_cast<std::uint8_t>(sumA);
        const auto plainB = static_cast<std::uint8_t>(encodedB ^ checksumA);
        const auto sumB = static_cast<unsigned>(checksumB) + plainB + (sumA > 0xff ? 1U : 0U);
        checksumB = static_cast<std::uint8_t>(sumB);
        const auto plainC = static_cast<std::uint8_t>(encodedC ^ checksumB);
        if (group != 174) {
            checksumC = static_cast<std::uint8_t>(static_cast<unsigned>(checksumC) + plainC
                + (sumB > 0xff ? 1U : 0U));
        }
        const auto output = group * 3;
        payload[output] = static_cast<char>(plainA);
        payload[output + 1] = static_cast<char>(plainB);
        if (group != 174) payload[output + 2] = static_cast<char>(plainC);
    }

    const auto packed = decodeGcr(static_cast<std::uint8_t>(encoded[input++]));
    const auto checkA = decodeGcr(static_cast<std::uint8_t>(encoded[input++]));
    const auto checkB = decodeGcr(static_cast<std::uint8_t>(encoded[input++]));
    const auto checkC = decodeGcr(static_cast<std::uint8_t>(encoded[input++]));
    if (packed < 0 || checkA < 0 || checkB < 0 || checkC < 0) return false;
    return static_cast<std::uint8_t>(checkA | ((packed & 0x30) << 2)) == checksumA
        && static_cast<std::uint8_t>(checkB | ((packed & 0x0c) << 4)) == checksumB
        && static_cast<std::uint8_t>(checkC | ((packed & 0x03) << 6)) == checksumC;
}

[[nodiscard]] int sectorsForTrack(int track)
{
    if (track < 16) {
        return 12;
    }
    if (track < 32) {
        return 11;
    }
    if (track < 48) {
        return 10;
    }
    if (track < 64) {
        return 9;
    }
    return 8;
}

[[nodiscard]] qsizetype firstBlockForTrack(int track, int side, bool doubleSided)
{
    qsizetype block = 0;
    const auto heads = doubleSided ? 2 : 1;
    for (int t = 0; t < track; ++t) {
        block += sectorsForTrack(t) * heads;
    }
    return block + (doubleSided ? side * sectorsForTrack(track) : 0);
}

[[nodiscard]] QVector<int> interleavedSectorOrder(int sectorsOnTrack)
{
    QVector<int> order(sectorsOnTrack, 0);
    auto slot = 0;
    for (int logicalSector = 0; logicalSector < sectorsOnTrack; ++logicalSector) {
        order[slot] = logicalSector;
        slot = (slot + 2) % sectorsOnTrack;
        if (slot == 0) {
            ++slot;
        }
    }
    return order;
}

void appendRepeated(QByteArray& bytes, std::uint8_t value, int count)
{
    for (int i = 0; i < count; ++i) {
        bytes.append(static_cast<char>(value));
    }
}

std::uint16_t mfmCrc(const QByteArray& bytes)
{
    std::uint16_t crc = 0xffff;
    for (const auto byte : bytes) {
        crc ^= static_cast<std::uint16_t>(static_cast<std::uint8_t>(byte)) << 8;
        for (int bit = 0; bit < 8; ++bit) {
            crc = static_cast<std::uint16_t>((crc & 0x8000) != 0 ? (crc << 1) ^ 0x1021 : crc << 1);
        }
    }
    return crc;
}

void appendMfmByte(QByteArray& bytes, QVector<bool>& marks, std::uint8_t value, bool mark = false)
{
    bytes.append(static_cast<char>(value));
    marks.append(mark);
}

void appendMfmRepeated(QByteArray& bytes, QVector<bool>& marks, std::uint8_t value, int count)
{
    for (int i = 0; i < count; ++i) appendMfmByte(bytes, marks, value);
}

void appendMfmField(QByteArray& bytes, QVector<bool>& marks, std::uint8_t type, const QByteArray& payload,
    std::uint8_t syncMark = 0xa1)
{
    QByteArray crcBytes;
    for (int i = 0; i < 3; ++i) {
        appendMfmByte(bytes, marks, syncMark, true);
        crcBytes.append(static_cast<char>(syncMark));
    }
    appendMfmByte(bytes, marks, type);
    crcBytes.append(static_cast<char>(type));
    for (const auto byte : payload) {
        appendMfmByte(bytes, marks, static_cast<std::uint8_t>(byte));
        crcBytes.append(byte);
    }
    const auto crc = mfmCrc(crcBytes);
    appendMfmByte(bytes, marks, static_cast<std::uint8_t>(crc >> 8));
    appendMfmByte(bytes, marks, static_cast<std::uint8_t>(crc));
}

void appendGcr(QByteArray& bytes, std::uint8_t value)
{
    bytes.append(static_cast<char>(gcrEncodeTable[value & 0x3f]));
}

std::uint8_t rol1(std::uint8_t value)
{
    return static_cast<std::uint8_t>((value << 1) | (value >> 7));
}

void appendEncodedPayload(QByteArray& output, const QByteArray& payload)
{
    std::uint8_t checksumA = 0;
    std::uint8_t checksumB = 0;
    std::uint8_t checksumC = 0;

    for (qsizetype i = 0; i < 175; ++i) {
        const auto offset = i * 3;
        const auto plainA = static_cast<std::uint8_t>(payload[offset]);
        const auto plainB = static_cast<std::uint8_t>(payload[offset + 1]);
        const auto plainC = i != 174 ? static_cast<std::uint8_t>(payload[offset + 2]) : std::uint8_t { 0 };

        checksumC = rol1(checksumC);

        const auto sumA = static_cast<unsigned>(checksumA) + plainA + (checksumC & 0x01);
        checksumA = static_cast<std::uint8_t>(sumA);
        const auto encodedA = static_cast<std::uint8_t>(plainA ^ checksumC);

        const auto sumB = static_cast<unsigned>(checksumB) + plainB + (sumA > 0xff ? 1U : 0U);
        checksumB = static_cast<std::uint8_t>(sumB);
        const auto encodedB = static_cast<std::uint8_t>(plainB ^ checksumA);

        if (i != 174) {
            const auto sumC = static_cast<unsigned>(checksumC) + plainC + (sumB > 0xff ? 1U : 0U);
            checksumC = static_cast<std::uint8_t>(sumC);
        }
        const auto encodedC = static_cast<std::uint8_t>(plainC ^ checksumB);

        appendGcr(output, static_cast<std::uint8_t>((encodedA >> 2) & 0x30)
                | static_cast<std::uint8_t>((encodedB >> 4) & 0x0c)
                | static_cast<std::uint8_t>((encodedC >> 6) & 0x03));
        appendGcr(output, encodedA);
        appendGcr(output, encodedB);
        if (i != 174) {
            appendGcr(output, encodedC);
        }
    }

    appendGcr(output, static_cast<std::uint8_t>((checksumA >> 2) & 0x30)
            | static_cast<std::uint8_t>((checksumB >> 4) & 0x0c)
            | static_cast<std::uint8_t>((checksumC >> 6) & 0x03));
    appendGcr(output, checksumA);
    appendGcr(output, checksumB);
    appendGcr(output, checksumC);
}

} // namespace

bool FloppyDiskImage::load(const QString& path, bool readOnly)
{
    m_forceReadOnly = readOnly;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return false;
    }

    const auto bytes = file.readAll();
    if (bytes.size() == raw400KBytes) {
        return loadRaw(path, bytes, Kind::Raw400K);
    }
    if (bytes.size() == raw800KBytes) {
        return loadRaw(path, bytes, Kind::Raw800K);
    }
    if (bytes.size() == raw1440KBytes) {
        return loadRaw(path, bytes, Kind::Raw1440K);
    }
    const auto truncatedKind = truncatedRawHfsKind(bytes);
    if (truncatedKind == Kind::Raw800K || truncatedKind == Kind::Raw1440K) {
        auto padded = bytes;
        padded.resize(truncatedKind == Kind::Raw800K ? raw800KBytes : raw1440KBytes);
        return loadRaw(path, padded, truncatedKind);
    }
    if (bytes.size() >= 84) {
        return loadDiskCopy42(path, bytes);
    }

    return false;
}

void FloppyDiskImage::eject()
{
    (void)flushWrites();
    m_mediaChanged = m_kind != Kind::Empty || m_mediaChanged;
    m_path.clear();
    m_kind = Kind::Empty;
    m_data.clear();
    m_tags.clear();
    m_diskCopyHeader.clear();
    m_writable = false;
    m_doubleSided = false;
    m_highDensity = false;
    m_motorOn = false;
    m_currentTrack = 0;
    m_currentSide = 0;
    invalidateTrackCache();
    m_writeBytes.clear();
    m_writeMarks.clear();
    m_writeMfmSector = -1;
    m_writeFailed = false;
}

bool FloppyDiskImage::inserted() const { return m_kind != Kind::Empty; }
bool FloppyDiskImage::writable() const { return m_writable; }
bool FloppyDiskImage::doubleSided() const { return m_doubleSided; }
bool FloppyDiskImage::highDensity() const { return m_highDensity; }
bool FloppyDiskImage::mediaChanged() const { return m_mediaChanged; }
FloppyDiskImage::Kind FloppyDiskImage::kind() const { return m_kind; }
QString FloppyDiskImage::path() const { return m_path; }
std::uint32_t FloppyDiskImage::blockCount() const { return static_cast<std::uint32_t>(m_data.size() / bytesPerSector); }
int FloppyDiskImage::trackCount() const { return tracksPerSide; }
int FloppyDiskImage::currentTrack() const { return m_currentTrack; }
int FloppyDiskImage::currentSide() const { return m_currentSide; }
bool FloppyDiskImage::motorOn() const { return m_motorOn; }
bool FloppyDiskImage::trackZero() const { return m_currentTrack == 0; }

void FloppyDiskImage::clearMediaChanged()
{
    m_mediaChanged = false;
}

QString FloppyDiskImage::formatName() const
{
    switch (m_kind) {
    case Kind::Raw400K:
        return QStringLiteral("raw-400k");
    case Kind::Raw800K:
        return QStringLiteral("raw-800k");
    case Kind::Raw1440K:
        return QStringLiteral("raw-1440k");
    case Kind::DiskCopy42:
        return QStringLiteral("diskcopy-4.2");
    case Kind::Empty:
        return QStringLiteral("empty");
    }
    return QStringLiteral("unknown");
}

void FloppyDiskImage::setCurrentTrack(int track)
{
    const auto clamped = std::clamp(track, 0, tracksPerSide - 1);
    if (m_currentTrack != clamped) {
        (void)flushWrites();
        m_currentTrack = clamped;
        invalidateTrackCache();
    }
}

void FloppyDiskImage::step(bool towardTrackZero)
{
    setCurrentTrack(m_currentTrack + (towardTrackZero ? -1 : 1));
}

void FloppyDiskImage::setCurrentSide(int side)
{
    const auto clamped = m_doubleSided ? std::clamp(side, 0, 1) : 0;
    if (m_currentSide != clamped) {
        (void)flushWrites();
        m_currentSide = clamped;
        invalidateTrackCache();
    }
}

void FloppyDiskImage::setMotorOn(bool on)
{
    if (m_motorOn && !on) (void)flushWrites();
    m_motorOn = on;
}

std::uint8_t FloppyDiskImage::nextNibble()
{
    return nextDiskByte().value;
}

FloppyDiskImage::DiskByte FloppyDiskImage::peekDiskByte()
{
    if (!inserted() || !m_motorOn) {
        return {};
    }
    if (m_trackCache.bytes.isEmpty() || m_trackCache.track != m_currentTrack || m_trackCache.side != m_currentSide) {
        rebuildTrackCache();
    }
    if (m_trackCache.bytes.isEmpty()) {
        return {};
    }
    return {
        static_cast<std::uint8_t>(m_trackCache.bytes[m_trackCache.cursor]),
        m_trackCache.cursor < m_trackCache.marks.size() && m_trackCache.marks[m_trackCache.cursor],
    };
}

FloppyDiskImage::DiskByte FloppyDiskImage::nextDiskByte()
{
    const auto diskByte = peekDiskByte();
    if (!inserted() || !m_motorOn || m_trackCache.bytes.isEmpty()) return diskByte;
    const auto value = diskByte.value;
    m_trackCache.cursor = (m_trackCache.cursor + 1) % m_trackCache.bytes.size();
    if (m_traceEnabled) {
        m_lastNibbles.append(static_cast<char>(value));
        if (m_lastNibbles.size() > maxLastNibbles) {
            m_lastNibbles.remove(0, m_lastNibbles.size() - maxLastNibbles);
        }
        m_traceShift = ((m_traceShift << 8) | value) & 0x00ffffff;
        if (m_traceShift == 0x00d5aa96 || m_traceShift == 0x00d5aaad || m_traceShift == 0x00deaaff) {
            if (m_traceEvents.size() == maxTraceEvents) {
                m_traceEvents.removeFirst();
            }
            const auto mark = m_traceShift == 0x00d5aa96 ? QStringLiteral("addr")
                : m_traceShift == 0x00d5aaad              ? QStringLiteral("data")
                                                          : QStringLiteral("trailer");
            m_traceEvents.append(QStringLiteral("floppy mark=%1 track=%2 side=%3 cursor=%4")
                                     .arg(mark)
                                     .arg(m_currentTrack)
                                     .arg(m_currentSide)
                                     .arg(m_trackCache.cursor));
        }
    }
    return diskByte;
}

bool FloppyDiskImage::writeDiskByte(std::uint8_t value, bool mark)
{
    if (!m_writable || !inserted() || !m_motorOn) {
        m_writeFailed = true;
        return false;
    }
    if (m_trackCache.bytes.isEmpty() || m_trackCache.track != m_currentTrack
        || m_trackCache.side != m_currentSide) {
        rebuildTrackCache();
    }
    if (m_highDensity && m_writeBytes.isEmpty() && m_writeMfmSector < 0)
        m_writeMfmSector = inferMfmSectorAtCursor();
    m_writeBytes.append(static_cast<char>(value));
    m_writeMarks.append(mark);
    if (!m_trackCache.bytes.isEmpty())
        m_trackCache.cursor = (m_trackCache.cursor + 1) % m_trackCache.bytes.size();
    const auto committed = m_highDensity ? processMfmWrites() : processGcrWrites();
    constexpr qsizetype maximumWriteWindow = 16384;
    if (m_writeBytes.size() > maximumWriteWindow) {
        const auto remove = m_writeBytes.size() - maximumWriteWindow;
        m_writeBytes.remove(0, remove);
        m_writeMarks.remove(0, remove);
    }
    return committed || !m_writeFailed;
}

bool FloppyDiskImage::flushWrites()
{
    if (!m_writeBytes.isEmpty()) {
        if (m_highDensity) (void)processMfmWrites();
        else (void)processGcrWrites();
    }
    m_writeBytes.clear();
    m_writeMarks.clear();
    m_writeMfmSector = -1;
    const auto ok = !m_writeFailed;
    m_writeFailed = false;
    return ok;
}

bool FloppyDiskImage::processGcrWrites()
{
    bool committed = false;
    for (;;) {
        const auto prologue = m_writeBytes.indexOf(QByteArray::fromHex("d5aaad"));
        if (prologue < 0) {
            if (m_writeBytes.size() > 2) {
                m_writeBytes.remove(0, m_writeBytes.size() - 2);
                m_writeMarks.remove(0, m_writeMarks.size() - 2);
            }
            return committed;
        }
        if (prologue > 0) {
            m_writeBytes.remove(0, prologue);
            m_writeMarks.remove(0, prologue);
        }
        constexpr qsizetype recordBytes = 710;
        if (m_writeBytes.size() < recordBytes) return committed;
        if (m_writeBytes.mid(707, 3) != QByteArray::fromHex("deaaff")) {
            m_writeBytes.remove(0, 1);
            m_writeMarks.remove(0, 1);
            continue;
        }
        const auto sector = decodeGcr(static_cast<std::uint8_t>(m_writeBytes[3]));
        QByteArray payload;
        if (sector >= 0 && sector < sectorsForTrack(m_currentTrack)
            && decodeGcrPayload(m_writeBytes.mid(4, 703), payload)) {
            committed |= commitSector(sector, payload.mid(tagBytesPerSector, bytesPerSector),
                payload.left(tagBytesPerSector));
        } else {
            m_writeFailed = true;
        }
        m_writeBytes.remove(0, recordBytes);
        m_writeMarks.remove(0, recordBytes);
    }
}

bool FloppyDiskImage::processMfmWrites()
{
    bool committed = false;
    for (;;) {
        const auto prologue = m_writeBytes.indexOf(QByteArray::fromHex("a1a1a1"));
        if (prologue < 0) {
            if (m_writeBytes.size() > 2) {
                m_writeBytes.remove(0, m_writeBytes.size() - 2);
                m_writeMarks.remove(0, m_writeMarks.size() - 2);
            }
            return committed;
        }
        if (prologue > 0) {
            m_writeBytes.remove(0, prologue);
            m_writeMarks.remove(0, prologue);
        }
        if (m_writeBytes.size() < 4) return committed;
        const auto type = static_cast<std::uint8_t>(m_writeBytes[3]);
        if (type == 0xfe) {
            constexpr qsizetype addressBytes = 10;
            if (m_writeBytes.size() < addressBytes) return committed;
            const auto field = m_writeBytes.left(8);
            const auto expected = mfmCrc(field);
            const auto actual = static_cast<std::uint16_t>(static_cast<std::uint8_t>(m_writeBytes[8]) << 8)
                | static_cast<std::uint8_t>(m_writeBytes[9]);
            if (expected == actual && static_cast<std::uint8_t>(m_writeBytes[4]) == m_currentTrack
                && static_cast<std::uint8_t>(m_writeBytes[5]) == m_currentSide
                && static_cast<std::uint8_t>(m_writeBytes[7]) == 2) {
                m_writeMfmSector = static_cast<int>(static_cast<std::uint8_t>(m_writeBytes[6])) - 1;
            } else if (m_traceEnabled) {
                m_traceEvents.append(QStringLiteral("MFM rejected ID field track=%1 side=%2 crc=%3/%4")
                                         .arg(m_currentTrack).arg(m_currentSide).arg(actual, 4, 16, QLatin1Char('0'))
                                         .arg(expected, 4, 16, QLatin1Char('0')));
            }
            m_writeBytes.remove(0, addressBytes);
            m_writeMarks.remove(0, addressBytes);
            continue;
        }
        if (type == 0xfb || type == 0xf8) {
            constexpr qsizetype dataRecordBytes = 3 + 1 + bytesPerSector + 2;
            if (m_writeBytes.size() < dataRecordBytes) return committed;
            const auto expected = mfmCrc(m_writeBytes.left(4 + bytesPerSector));
            const auto actual = static_cast<std::uint16_t>(
                                    static_cast<std::uint8_t>(m_writeBytes[4 + bytesPerSector]) << 8)
                | static_cast<std::uint8_t>(m_writeBytes[5 + bytesPerSector]);
            if (m_writeMfmSector >= 0 && m_writeMfmSector < 18 && expected == actual)
                committed |= commitSector(m_writeMfmSector, m_writeBytes.mid(4, bytesPerSector));
            else {
                m_writeFailed = true;
                if (m_traceEnabled)
                    m_traceEvents.append(QStringLiteral("MFM rejected data field sector=%1 crc=%2/%3")
                                             .arg(m_writeMfmSector).arg(actual, 4, 16, QLatin1Char('0'))
                                             .arg(expected, 4, 16, QLatin1Char('0')));
            }
            m_writeBytes.remove(0, dataRecordBytes);
            m_writeMarks.remove(0, dataRecordBytes);
            continue;
        }
        m_writeBytes.remove(0, 1);
        m_writeMarks.remove(0, 1);
    }
}

bool FloppyDiskImage::commitSector(int logicalSector, const QByteArray& data, const QByteArray& tags)
{
    if (data.size() != bytesPerSector) return false;
    qsizetype block = 0;
    if (m_highDensity)
        block = (m_currentTrack * 2 + m_currentSide) * 18 + logicalSector;
    else
        block = firstBlockForTrack(m_currentTrack, m_currentSide, m_doubleSided) + logicalSector;
    if (block < 0 || block >= m_data.size() / bytesPerSector) return false;
    std::copy(data.begin(), data.end(), m_data.begin() + block * bytesPerSector);
    if (!tags.isEmpty() && block < m_tags.size() && tags.size() == tagBytesPerSector)
        m_tags[static_cast<int>(block)] = tags;
    if (!persistImage()) {
        m_writeFailed = true;
        return false;
    }
    invalidateTrackCache();
    return true;
}

bool FloppyDiskImage::persistImage()
{
    if (!m_writable || m_path.isEmpty()) return false;
    QByteArray output;
    if (m_kind == Kind::DiskCopy42) {
        output = m_diskCopyHeader;
        if (output.size() != 84) return false;
        output.append(m_data);
        QByteArray tagBytes;
        for (const auto& tag : m_tags) tagBytes.append(tag);
        output.append(tagBytes);
        writeBe32(output, 72, diskCopyChecksum(m_data));
        // Disk Copy excludes the first sector's 12-byte tag from its tag checksum.
        writeBe32(output, 76, diskCopyChecksum(tagBytes.mid(tagBytesPerSector)));
    } else {
        output = m_data;
    }
    QSaveFile file(m_path);
    if (!file.open(QIODevice::WriteOnly) || file.write(output) != output.size()) return false;
    return file.commit();
}

int FloppyDiskImage::inferMfmSectorAtCursor() const
{
    if (m_trackCache.bytes.isEmpty()) return -1;
    int sector = -1;
    const auto limit = std::min(m_trackCache.cursor, m_trackCache.bytes.size());
    for (qsizetype offset = 0; offset + 7 < limit; ++offset) {
        if (m_trackCache.bytes.mid(offset, 4) == QByteArray::fromHex("a1a1a1fe"))
            sector = static_cast<int>(static_cast<std::uint8_t>(m_trackCache.bytes[offset + 6])) - 1;
    }
    return sector;
}

void FloppyDiskImage::invalidateTrackCache()
{
    m_trackCache = {};
}

FloppyDiskImage::DebugState FloppyDiskImage::debugState() const
{
    return {
        m_path,
        formatName(),
        inserted(),
        m_writable,
        m_doubleSided,
        m_highDensity,
        m_motorOn,
        m_currentTrack,
        m_currentSide,
        m_trackCache.bytes.size(),
        m_trackCache.cursor,
    };
}

QByteArray FloppyDiskImage::trackBytesForDebug(int track, int side) const
{
    const auto clampedTrack = std::clamp(track, 0, tracksPerSide - 1);
    const auto clampedSide = m_doubleSided ? std::clamp(side, 0, 1) : 0;
    if (!m_highDensity) return buildTrackBytes(clampedTrack, clampedSide);
    QByteArray bytes;
    QVector<bool> marks;
    buildMfmTrack(clampedTrack, clampedSide, bytes, marks);
    return bytes;
}

void FloppyDiskImage::setTraceEnabled(bool enabled)
{
    m_traceEnabled = enabled;
}

void FloppyDiskImage::clearTrace()
{
    m_traceShift = 0;
    m_traceEvents.clear();
    m_lastNibbles.clear();
}

QStringList FloppyDiskImage::traceEvents() const
{
    return m_traceEvents;
}

QByteArray FloppyDiskImage::lastNibblesForDebug() const
{
    return m_lastNibbles;
}

bool FloppyDiskImage::loadRaw(const QString& path, const QByteArray& bytes, Kind kind)
{
    const auto previousKind = m_kind;
    const auto previousPath = m_path;
    m_path = path;
    m_kind = kind;
    m_data = bytes;
    m_tags.clear();
    m_diskCopyHeader.clear();
    m_writable = !m_forceReadOnly && QFileInfo(path).isWritable();
    m_doubleSided = kind != Kind::Raw400K;
    m_highDensity = kind == Kind::Raw1440K;
    m_currentTrack = 0;
    m_currentSide = 0;
    m_mediaChanged = previousKind != Kind::Empty || previousPath != path || m_mediaChanged;
    invalidateTrackCache();
    m_writeBytes.clear();
    m_writeMarks.clear();
    m_writeMfmSector = -1;
    m_writeFailed = false;
    return true;
}

bool FloppyDiskImage::loadDiskCopy42(const QString& path, const QByteArray& bytes)
{
    const auto dataSize = readBe32(bytes, 64);
    const auto tagSize = readBe32(bytes, 68);
    if ((dataSize != raw400KBytes && dataSize != raw800KBytes && dataSize != raw1440KBytes)
        || bytes.size() < static_cast<qsizetype>(84 + dataSize + tagSize)) {
        return false;
    }
    if (tagSize != 0 && tagSize != static_cast<std::uint32_t>((dataSize / bytesPerSector) * tagBytesPerSector)) {
        return false;
    }

    const auto previousKind = m_kind;
    const auto previousPath = m_path;
    m_path = path;
    m_kind = Kind::DiskCopy42;
    m_diskCopyHeader = bytes.left(84);
    m_data = bytes.mid(84, static_cast<qsizetype>(dataSize));
    m_tags.clear();
    if (tagSize != 0) {
        const auto tagBytes = bytes.mid(84 + static_cast<qsizetype>(dataSize), static_cast<qsizetype>(tagSize));
        const auto blocks = static_cast<int>(tagSize / tagBytesPerSector);
        m_tags.reserve(blocks);
        for (int block = 0; block < blocks; ++block) {
            m_tags.append(tagBytes.mid(block * tagBytesPerSector, tagBytesPerSector));
        }
    }
    m_writable = !m_forceReadOnly && QFileInfo(path).isWritable();
    m_doubleSided = dataSize != raw400KBytes;
    m_highDensity = dataSize == raw1440KBytes;
    m_currentTrack = 0;
    m_currentSide = 0;
    m_mediaChanged = previousKind != Kind::Empty || previousPath != path || m_mediaChanged;
    invalidateTrackCache();
    m_writeBytes.clear();
    m_writeMarks.clear();
    m_writeMfmSector = -1;
    m_writeFailed = false;
    return true;
}

void FloppyDiskImage::rebuildTrackCache()
{
    m_trackCache.track = m_currentTrack;
    m_trackCache.side = m_currentSide;
    m_trackCache.cursor = 0;
    m_trackCache.bytes.clear();
    m_trackCache.marks.clear();
    if (m_highDensity) buildMfmTrack(m_currentTrack, m_currentSide, m_trackCache.bytes, m_trackCache.marks);
    else m_trackCache.bytes = buildTrackBytes(m_currentTrack, m_currentSide);
}

void FloppyDiskImage::buildMfmTrack(int physicalTrack, int side, QByteArray& bytes, QVector<bool>& marks) const
{
    constexpr int sectorsPerTrack = 18;
    constexpr int bytesPerRevolution = 12500; // 500 kbit/s at 300 RPM.
    appendMfmRepeated(bytes, marks, 0x4e, 80);
    appendMfmRepeated(bytes, marks, 0x00, 12);
    appendMfmField(bytes, marks, 0xfc, {}, 0xc2);
    appendMfmRepeated(bytes, marks, 0x4e, 50);
    for (int sector = 0; sector < sectorsPerTrack; ++sector) {
        appendMfmRepeated(bytes, marks, 0x00, 12);
        QByteArray address;
        address.append(static_cast<char>(physicalTrack));
        address.append(static_cast<char>(side));
        address.append(static_cast<char>(sector + 1));
        address.append(static_cast<char>(2)); // 512-byte sectors
        appendMfmField(bytes, marks, 0xfe, address);
        appendMfmRepeated(bytes, marks, 0x4e, 22);
        appendMfmRepeated(bytes, marks, 0x00, 12);
        const auto block = (physicalTrack * 2 + side) * sectorsPerTrack + sector;
        appendMfmField(bytes, marks, 0xfb, m_data.mid(block * bytesPerSector, bytesPerSector));
        appendMfmRepeated(bytes, marks, 0x4e, 101);
    }
    appendMfmRepeated(bytes, marks, 0x4e, bytesPerRevolution - bytes.size());
}

QByteArray FloppyDiskImage::buildTrackBytes(int physicalTrack, int side) const
{
    QByteArray bytes;
    if (!inserted()) {
        return bytes;
    }

    const auto sectorsOnTrack = sectorsForTrack(physicalTrack);
    appendRepeated(bytes, 0xff, 48);
    for (const auto logicalSector : interleavedSectorOrder(sectorsOnTrack)) {
        appendSector(bytes, physicalTrack, side, logicalSector);
    }
    appendRepeated(bytes, 0xff, 128);
    return bytes;
}

void FloppyDiskImage::appendSector(QByteArray& trackBytes, int physicalTrack, int side, int logicalSector) const
{
    const auto formatByte = static_cast<std::uint8_t>(m_doubleSided ? 0x22 : 0x02);
    const auto trackLow = static_cast<std::uint8_t>(physicalTrack & 0x3f);
    const auto sideAndTrackHigh = static_cast<std::uint8_t>(((physicalTrack >> 6) & 0x01) | (side != 0 ? 0x20 : 0x00));
    const auto addressChecksum = static_cast<std::uint8_t>(formatByte ^ trackLow ^ sideAndTrackHigh ^ logicalSector);

    appendRepeated(trackBytes, 0xff, 18);
    trackBytes.append(static_cast<char>(0xd5));
    trackBytes.append(static_cast<char>(0xaa));
    trackBytes.append(static_cast<char>(0x96));
    appendGcr(trackBytes, trackLow);
    appendGcr(trackBytes, logicalSector);
    appendGcr(trackBytes, sideAndTrackHigh);
    appendGcr(trackBytes, formatByte);
    appendGcr(trackBytes, addressChecksum);
    trackBytes.append(static_cast<char>(0xde));
    trackBytes.append(static_cast<char>(0xaa));
    trackBytes.append(static_cast<char>(0xff));

    appendRepeated(trackBytes, 0xff, 14);
    trackBytes.append(static_cast<char>(0xd5));
    trackBytes.append(static_cast<char>(0xaa));
    trackBytes.append(static_cast<char>(0xad));
    appendGcr(trackBytes, logicalSector);
    appendEncodedPayload(trackBytes, sectorPayload(physicalTrack, side, logicalSector));
    trackBytes.append(static_cast<char>(0xde));
    trackBytes.append(static_cast<char>(0xaa));
    trackBytes.append(static_cast<char>(0xff));
    appendRepeated(trackBytes, 0xff, 8);
}

QByteArray FloppyDiskImage::sectorPayload(int physicalTrack, int side, int logicalSector) const
{
    const auto block = firstBlockForTrack(physicalTrack, side, m_doubleSided) + logicalSector;
    QByteArray payload;
    payload.resize(encodedPayloadBytes);
    payload.fill(0);

    if (block >= 0 && block < static_cast<qsizetype>(m_data.size() / bytesPerSector)) {
        const auto tagIndex = static_cast<int>(block);
        if (tagIndex < m_tags.size() && m_tags[tagIndex].size() == tagBytesPerSector) {
            std::copy(m_tags[tagIndex].begin(), m_tags[tagIndex].end(), payload.begin());
        }

        const auto dataOffset = block * bytesPerSector;
        std::copy(m_data.begin() + dataOffset, m_data.begin() + dataOffset + bytesPerSector, payload.begin() + tagBytesPerSector);
    }

    return payload;
}

} // namespace cutemac::devices::floppy
