#pragma once

#include <optional>

#include <QByteArray>
#include <QString>
#include <QStringList>
#include <QVector>

namespace cutemac::debug {

// A panic archive is an ordinary zip so a dump attached to a bug report opens
// with any unzip tool. Deflate handles mostly-zero RAM well enough that sparse
// page elision is not worth the reader complexity.

inline constexpr const char* panicArchiveExtension = "cutemacpanic";
inline constexpr int panicArchiveSchemaVersion = 1;

struct PanicArchiveMember {
    QString name;
    QByteArray contents;
};

class PanicArchiveWriter {
public:
    explicit PanicArchiveWriter(QString path);
    ~PanicArchiveWriter();

    PanicArchiveWriter(const PanicArchiveWriter&) = delete;
    PanicArchiveWriter& operator=(const PanicArchiveWriter&) = delete;

    [[nodiscard]] bool open();
    void add(const QString& name, const QByteArray& contents);
    // Commits every queued member. The queued contents are held until close()
    // because libzip reads its sources lazily during the final write.
    [[nodiscard]] bool close();

    [[nodiscard]] QString path() const { return m_path; }
    [[nodiscard]] QString error() const { return m_error; }
    [[nodiscard]] int memberCount() const { return static_cast<int>(m_members.size()); }

private:
    QString m_path;
    QString m_error;
    QVector<PanicArchiveMember> m_members;
    void* m_archive = nullptr;
    bool m_closed = false;
};

class PanicArchiveReader {
public:
    explicit PanicArchiveReader(QString path);
    ~PanicArchiveReader();

    PanicArchiveReader(const PanicArchiveReader&) = delete;
    PanicArchiveReader& operator=(const PanicArchiveReader&) = delete;

    [[nodiscard]] bool open();
    [[nodiscard]] QStringList memberNames() const;
    [[nodiscard]] bool contains(const QString& name) const;
    [[nodiscard]] std::optional<QByteArray> read(const QString& name) const;

    [[nodiscard]] QString path() const { return m_path; }
    [[nodiscard]] QString error() const { return m_error; }

private:
    QString m_path;
    mutable QString m_error;
    void* m_archive = nullptr;
};

} // namespace cutemac::debug
