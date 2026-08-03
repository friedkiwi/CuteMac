#pragma once

#include <cstdint>

#include <QByteArray>
#include <QString>
#include <QStringList>
#include <QVector>

namespace cutemac::devices::floppy {

class FloppyDiskImage {
public:
    struct DiskByte {
        std::uint8_t value = 0;
        bool mark = false;
    };

    struct DebugState {
        QString imagePath;
        QString imageFormat;
        bool inserted = false;
        bool writable = false;
        bool doubleSided = false;
        bool highDensity = false;
        bool motorOn = false;
        int track = 0;
        int side = 0;
        qsizetype trackBytes = 0;
        qsizetype trackCursor = 0;
    };

    enum class Kind {
        Empty,
        Raw400K,
        Raw800K,
        Raw1440K,
        DiskCopy42,
    };

    [[nodiscard]] bool load(const QString& path, bool readOnly = false);
    void eject();

    [[nodiscard]] bool inserted() const;
    [[nodiscard]] bool writable() const;
    [[nodiscard]] bool doubleSided() const;
    [[nodiscard]] bool highDensity() const;
    [[nodiscard]] bool mediaChanged() const;
    [[nodiscard]] Kind kind() const;
    [[nodiscard]] QString path() const;
    [[nodiscard]] QString formatName() const;
    [[nodiscard]] std::uint32_t blockCount() const;
    [[nodiscard]] int trackCount() const;
    [[nodiscard]] int currentTrack() const;
    [[nodiscard]] int currentSide() const;

    void setCurrentTrack(int track);
    void step(bool towardTrackZero);
    void setCurrentSide(int side);
    void setMotorOn(bool on);
    [[nodiscard]] bool motorOn() const;
    [[nodiscard]] bool trackZero() const;
    void clearMediaChanged();

    [[nodiscard]] std::uint8_t nextNibble();
    [[nodiscard]] DiskByte nextDiskByte();
    [[nodiscard]] DiskByte peekDiskByte();
    [[nodiscard]] bool writeDiskByte(std::uint8_t value, bool mark = false);
    [[nodiscard]] bool flushWrites();
    void invalidateTrackCache();
    [[nodiscard]] DebugState debugState() const;
    [[nodiscard]] QByteArray trackBytesForDebug(int track, int side) const;
    void setTraceEnabled(bool enabled);
    void clearTrace();
    [[nodiscard]] QStringList traceEvents() const;
    [[nodiscard]] QByteArray lastNibblesForDebug() const;

private:
    struct TrackCache {
        int track = -1;
        int side = -1;
        QByteArray bytes;
        QVector<bool> marks;
        qsizetype cursor = 0;
    };

    [[nodiscard]] bool loadRaw(const QString& path, const QByteArray& bytes, Kind kind);
    [[nodiscard]] bool loadDiskCopy42(const QString& path, const QByteArray& bytes);
    void rebuildTrackCache();
    [[nodiscard]] QByteArray buildTrackBytes(int physicalTrack, int side) const;
    void buildMfmTrack(int physicalTrack, int side, QByteArray& bytes, QVector<bool>& marks) const;
    void appendSector(QByteArray& trackBytes, int physicalTrack, int side, int logicalSector) const;
    [[nodiscard]] QByteArray sectorPayload(int physicalTrack, int side, int logicalSector) const;
    [[nodiscard]] bool processGcrWrites();
    [[nodiscard]] bool processMfmWrites();
    [[nodiscard]] bool commitSector(int logicalSector, const QByteArray& data, const QByteArray& tags = {});
    [[nodiscard]] bool persistImage();
    [[nodiscard]] int inferMfmSectorAtCursor() const;

    QString m_path;
    Kind m_kind = Kind::Empty;
    QByteArray m_data;
    QVector<QByteArray> m_tags;
    QByteArray m_diskCopyHeader;
    bool m_writable = false;
    bool m_forceReadOnly = false;
    bool m_doubleSided = false;
    bool m_highDensity = false;
    bool m_mediaChanged = false;
    bool m_motorOn = false;
    int m_currentTrack = 0;
    int m_currentSide = 0;
    TrackCache m_trackCache;
    QByteArray m_writeBytes;
    QVector<bool> m_writeMarks;
    int m_writeMfmSector = -1;
    bool m_writeFailed = false;
    bool m_traceEnabled = false;
    std::uint32_t m_traceShift = 0;
    QStringList m_traceEvents;
    QByteArray m_lastNibbles;
};

} // namespace cutemac::devices::floppy
