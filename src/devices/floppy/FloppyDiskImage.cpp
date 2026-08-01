#include "cutemac/devices/floppy/FloppyDiskImage.h"

#include <QFile>
#include <QFileInfo>

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
    if (bytes.size() >= 84) {
        return loadDiskCopy42(path, bytes);
    }

    return false;
}

void FloppyDiskImage::eject()
{
    m_path.clear();
    m_kind = Kind::Empty;
    m_data.clear();
    m_tags.clear();
    m_writable = false;
    m_doubleSided = false;
    m_motorOn = false;
    m_currentTrack = 0;
    m_currentSide = 0;
    invalidateTrackCache();
}

bool FloppyDiskImage::inserted() const { return m_kind != Kind::Empty; }
bool FloppyDiskImage::writable() const { return m_writable; }
bool FloppyDiskImage::doubleSided() const { return m_doubleSided; }
FloppyDiskImage::Kind FloppyDiskImage::kind() const { return m_kind; }
QString FloppyDiskImage::path() const { return m_path; }
std::uint32_t FloppyDiskImage::blockCount() const { return static_cast<std::uint32_t>(m_data.size() / bytesPerSector); }
int FloppyDiskImage::trackCount() const { return tracksPerSide; }
int FloppyDiskImage::currentTrack() const { return m_currentTrack; }
int FloppyDiskImage::currentSide() const { return m_currentSide; }
bool FloppyDiskImage::motorOn() const { return m_motorOn; }
bool FloppyDiskImage::trackZero() const { return m_currentTrack == 0; }

QString FloppyDiskImage::formatName() const
{
    switch (m_kind) {
    case Kind::Raw400K:
        return QStringLiteral("raw-400k");
    case Kind::Raw800K:
        return QStringLiteral("raw-800k");
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
        m_currentSide = clamped;
        invalidateTrackCache();
    }
}

void FloppyDiskImage::setMotorOn(bool on)
{
    m_motorOn = on;
}

std::uint8_t FloppyDiskImage::nextNibble()
{
    if (!inserted() || !m_motorOn) {
        return 0x00;
    }
    if (m_trackCache.bytes.isEmpty() || m_trackCache.track != m_currentTrack || m_trackCache.side != m_currentSide) {
        rebuildTrackCache();
    }
    if (m_trackCache.bytes.isEmpty()) {
        return 0x00;
    }

    const auto value = static_cast<std::uint8_t>(m_trackCache.bytes[m_trackCache.cursor]);
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
    return value;
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
        m_motorOn,
        m_currentTrack,
        m_currentSide,
        m_trackCache.bytes.size(),
        m_trackCache.cursor,
    };
}

QByteArray FloppyDiskImage::trackBytesForDebug(int track, int side) const
{
    return buildTrackBytes(std::clamp(track, 0, tracksPerSide - 1), m_doubleSided ? std::clamp(side, 0, 1) : 0);
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
    m_path = path;
    m_kind = kind;
    m_data = bytes;
    m_tags.clear();
    m_writable = !m_forceReadOnly && QFileInfo(path).isWritable();
    m_doubleSided = kind == Kind::Raw800K;
    m_currentTrack = 0;
    m_currentSide = 0;
    invalidateTrackCache();
    return true;
}

bool FloppyDiskImage::loadDiskCopy42(const QString& path, const QByteArray& bytes)
{
    const auto dataSize = readBe32(bytes, 64);
    const auto tagSize = readBe32(bytes, 68);
    if ((dataSize != raw400KBytes && dataSize != raw800KBytes) || bytes.size() < static_cast<qsizetype>(84 + dataSize + tagSize)) {
        return false;
    }
    if (tagSize != 0 && tagSize != static_cast<std::uint32_t>((dataSize / bytesPerSector) * tagBytesPerSector)) {
        return false;
    }

    m_path = path;
    m_kind = Kind::DiskCopy42;
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
    m_doubleSided = dataSize == raw800KBytes;
    m_currentTrack = 0;
    m_currentSide = 0;
    invalidateTrackCache();
    return true;
}

void FloppyDiskImage::rebuildTrackCache()
{
    m_trackCache.track = m_currentTrack;
    m_trackCache.side = m_currentSide;
    m_trackCache.cursor = 0;
    m_trackCache.bytes = buildTrackBytes(m_currentTrack, m_currentSide);
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
