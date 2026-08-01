#pragma once

#include <cstdint>

#include <QByteArray>
#include <QString>
#include <QVector>

namespace cutemac::devices::floppy {

class FloppyDiskImage {
public:
    enum class Kind {
        Empty,
        Raw400K,
        Raw800K,
        DiskCopy42,
    };

    [[nodiscard]] bool load(const QString& path);
    void eject();

    [[nodiscard]] bool inserted() const;
    [[nodiscard]] bool writable() const;
    [[nodiscard]] bool doubleSided() const;
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

    [[nodiscard]] std::uint8_t nextNibble();
    void invalidateTrackCache();

private:
    struct TrackCache {
        int track = -1;
        int side = -1;
        QByteArray bytes;
        qsizetype cursor = 0;
    };

    [[nodiscard]] bool loadRaw(const QString& path, const QByteArray& bytes, Kind kind);
    [[nodiscard]] bool loadDiskCopy42(const QString& path, const QByteArray& bytes);
    void rebuildTrackCache();
    void appendSector(QByteArray& trackBytes, int physicalTrack, int side, int logicalSector) const;
    [[nodiscard]] QByteArray sectorPayload(int physicalTrack, int side, int logicalSector) const;

    QString m_path;
    Kind m_kind = Kind::Empty;
    QByteArray m_data;
    QVector<QByteArray> m_tags;
    bool m_writable = false;
    bool m_doubleSided = false;
    bool m_motorOn = false;
    int m_currentTrack = 0;
    int m_currentSide = 0;
    TrackCache m_trackCache;
};

} // namespace cutemac::devices::floppy
