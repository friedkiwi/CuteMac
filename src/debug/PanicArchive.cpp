#include "cutemac/debug/PanicArchive.h"

#include <utility>

#include <zip.h>

namespace cutemac::debug {

namespace {

QString describe(zip_t* archive)
{
    if (archive == nullptr) return QStringLiteral("archive not open");
    return QString::fromUtf8(zip_strerror(archive));
}

} // namespace

PanicArchiveWriter::PanicArchiveWriter(QString path)
    : m_path(std::move(path))
{
}

PanicArchiveWriter::~PanicArchiveWriter()
{
    if (m_archive != nullptr && !m_closed) {
        zip_discard(static_cast<zip_t*>(m_archive));
        m_archive = nullptr;
    }
}

bool PanicArchiveWriter::open()
{
    int error = 0;
    auto* archive = zip_open(m_path.toUtf8().constData(), ZIP_CREATE | ZIP_TRUNCATE, &error);
    if (archive == nullptr) {
        zip_error_t details;
        zip_error_init_with_code(&details, error);
        m_error = QStringLiteral("cannot create %1: %2").arg(m_path, QString::fromUtf8(zip_error_strerror(&details)));
        zip_error_fini(&details);
        return false;
    }
    m_archive = archive;
    return true;
}

void PanicArchiveWriter::add(const QString& name, const QByteArray& contents)
{
    // Held rather than written now: libzip keeps a pointer into the buffer and
    // only reads it while zip_close() streams the archive out.
    m_members.append({ name, contents });
}

bool PanicArchiveWriter::close()
{
    if (m_archive == nullptr) {
        m_error = QStringLiteral("archive not open");
        return false;
    }
    auto* archive = static_cast<zip_t*>(m_archive);

    for (const auto& member : m_members) {
        auto* source = zip_source_buffer(archive, member.contents.constData(),
            static_cast<zip_uint64_t>(member.contents.size()), 0);
        if (source == nullptr) {
            m_error = QStringLiteral("cannot stage %1: %2").arg(member.name, describe(archive));
            zip_discard(archive);
            m_archive = nullptr;
            m_closed = true;
            return false;
        }
        const auto index = zip_file_add(archive, member.name.toUtf8().constData(), source, ZIP_FL_OVERWRITE);
        if (index < 0) {
            zip_source_free(source);
            m_error = QStringLiteral("cannot add %1: %2").arg(member.name, describe(archive));
            zip_discard(archive);
            m_archive = nullptr;
            m_closed = true;
            return false;
        }
        (void)zip_set_file_compression(archive, static_cast<zip_uint64_t>(index), ZIP_CM_DEFLATE, 6);
    }

    if (zip_close(archive) != 0) {
        m_error = QStringLiteral("cannot write %1: %2").arg(m_path, describe(archive));
        zip_discard(archive);
        m_archive = nullptr;
        m_closed = true;
        return false;
    }
    m_archive = nullptr;
    m_closed = true;
    return true;
}

PanicArchiveReader::PanicArchiveReader(QString path)
    : m_path(std::move(path))
{
}

PanicArchiveReader::~PanicArchiveReader()
{
    if (m_archive != nullptr) {
        zip_close(static_cast<zip_t*>(m_archive));
        m_archive = nullptr;
    }
}

bool PanicArchiveReader::open()
{
    int error = 0;
    auto* archive = zip_open(m_path.toUtf8().constData(), ZIP_RDONLY, &error);
    if (archive == nullptr) {
        zip_error_t details;
        zip_error_init_with_code(&details, error);
        m_error = QStringLiteral("cannot open %1: %2").arg(m_path, QString::fromUtf8(zip_error_strerror(&details)));
        zip_error_fini(&details);
        return false;
    }
    m_archive = archive;
    return true;
}

QStringList PanicArchiveReader::memberNames() const
{
    QStringList names;
    if (m_archive == nullptr) return names;
    auto* archive = static_cast<zip_t*>(m_archive);
    const auto count = zip_get_num_entries(archive, 0);
    for (zip_int64_t index = 0; index < count; ++index) {
        const auto* name = zip_get_name(archive, static_cast<zip_uint64_t>(index), 0);
        if (name != nullptr) names.append(QString::fromUtf8(name));
    }
    return names;
}

bool PanicArchiveReader::contains(const QString& name) const
{
    if (m_archive == nullptr) return false;
    return zip_name_locate(static_cast<zip_t*>(m_archive), name.toUtf8().constData(), 0) >= 0;
}

std::optional<QByteArray> PanicArchiveReader::read(const QString& name) const
{
    if (m_archive == nullptr) {
        m_error = QStringLiteral("archive not open");
        return std::nullopt;
    }
    auto* archive = static_cast<zip_t*>(m_archive);

    zip_stat_t stat;
    zip_stat_init(&stat);
    if (zip_stat(archive, name.toUtf8().constData(), 0, &stat) != 0) {
        m_error = QStringLiteral("no member %1").arg(name);
        return std::nullopt;
    }
    if ((stat.valid & ZIP_STAT_SIZE) == 0) {
        m_error = QStringLiteral("member %1 has no recorded size").arg(name);
        return std::nullopt;
    }

    auto* file = zip_fopen(archive, name.toUtf8().constData(), 0);
    if (file == nullptr) {
        m_error = QStringLiteral("cannot open member %1: %2").arg(name, describe(archive));
        return std::nullopt;
    }

    QByteArray contents;
    contents.resize(static_cast<qsizetype>(stat.size));
    const auto read = zip_fread(file, contents.data(), stat.size);
    zip_fclose(file);
    if (read < 0 || static_cast<zip_uint64_t>(read) != stat.size) {
        m_error = QStringLiteral("short read on member %1").arg(name);
        return std::nullopt;
    }
    return contents;
}

} // namespace cutemac::debug
