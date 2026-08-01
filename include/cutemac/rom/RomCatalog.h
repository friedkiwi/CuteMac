#pragma once

#include <QByteArray>
#include <QString>
#include <QStringList>
#include <QVector>

#include "cutemac/config/Configuration.h"

namespace cutemac::rom {

enum class RomKind { Machine, Device, Embedded };

struct RomDefinition {
    QString id;
    QString displayName;
    RomKind kind = RomKind::Machine;
    QString ownerId;
    QString revision;
    qsizetype expectedSize = 0;
    QByteArray sha256;
    QByteArray sha1;
    QByteArray headerChecksum;
    int preference = 0;
    bool discouraged = false;
};

struct RomStatus {
    RomDefinition definition;
    QString path;
    bool found = false;
};

// Central registry and resolver for every ROM known to CuteMac. User ROMs are
// identified by content, never by their filenames.
class RomCatalog {
public:
    explicit RomCatalog(QString directoryPath = {});

    [[nodiscard]] static QVector<RomDefinition> definitions();
    [[nodiscard]] QString directoryPath() const;
    void refresh();
    [[nodiscard]] QVector<RomStatus> statuses() const;
    [[nodiscard]] QString pathForId(const QString& romId) const;
    [[nodiscard]] QString preferredMachineRomPath(const QString& machineId) const;
    [[nodiscard]] QString deviceRomPath(const QString& deviceId) const;
    [[nodiscard]] QStringList requiredRomOwnerIds(const config::Configuration& configuration) const;
    [[nodiscard]] QStringList missingRomNames(const config::Configuration& configuration) const;
    [[nodiscard]] QString warningForConfiguration(const config::Configuration& configuration) const;

private:
    QString m_directoryPath;
    QVector<RomStatus> m_statuses;
};

} // namespace cutemac::rom
