#pragma once

#include <cstdint>

#include <QByteArray>
#include <QString>
#include <QStringList>
#include <QVector>

namespace cutemac::rom {

struct RomPatchEdit {
    std::uint32_t offset = 0;
    QByteArray expectedBytes;
    QByteArray replacementBytes;
};

struct RomPatchDefinition {
    QString id;
    QString description;
    QString machineId;
    QByteArray requiredSha256;
    QVector<RomPatchEdit> edits;
};

struct RomPatchResult {
    bool success = false;
    QByteArray originalSha256;
    QStringList appliedPatchIds;
    QString error;
};

class RomPatcher {
public:
    [[nodiscard]] static QVector<RomPatchDefinition> definitionsForMachine(const QString& machineId);
    [[nodiscard]] static RomPatchResult apply(
        QByteArray& romBytes,
        const QString& machineId,
        const QStringList& enabledPatchIds);
    [[nodiscard]] static RomPatchResult applyDefinitions(
        QByteArray& romBytes,
        const QString& machineId,
        const QStringList& enabledPatchIds,
        const QVector<RomPatchDefinition>& definitions);
};

} // namespace cutemac::rom
