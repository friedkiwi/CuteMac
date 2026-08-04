#include "cutemac/rom/RomPatcher.h"

#include <QCryptographicHash>

#include <algorithm>

namespace cutemac::rom {

namespace {

QVector<RomPatchDefinition> allDefinitions()
{
    return {
        {
            QStringLiteral("macplus.skip_ram_pattern_test"),
            QStringLiteral("Skip the destructive full-RAM pattern passes while preserving RAM sizing"),
            QStringLiteral("mac-plus"),
            QByteArray::fromHex("dd908e2b65772a6b1f0c859c24e9a0d3dcde17b1c6a24f4abd8955846d7895e7"),
            {
                { 0x000000, QByteArray::fromHex("4d1f8172"), QByteArray::fromHex("4d1f7a72") },
                { 0x000e08, QByteArray::fromHex("6722"), QByteArray::fromHex("6022") },
            },
        },
        {
            QStringLiteral("maciicx.skip_ram_pattern_test"),
            QStringLiteral("Skip the destructive full-RAM pattern passes while preserving RAM sizing"),
            QStringLiteral("mac-iicx"),
            QByteArray::fromHex("79fae48e2d5cfde68520e46616503963f8c16430903f410514b62c1379af20cb"),
            {
                { 0x00000000, QByteArray::fromHex("97221136"), QByteArray::fromHex("97223bc4") },
                { 0x00003714, QByteArray::fromHex("2448"), QByteArray::fromHex("4ed6") },
            },
        },
        {
            QStringLiteral("quadra700.skip_ram_pattern_test"),
            QStringLiteral("Skip the destructive full-RAM pattern passes while preserving RAM sizing"),
            QStringLiteral("quadra-700"),
            QByteArray::fromHex("c2093476e9c9a7d76973910a91fbfba23ca71163b84eb2623adf8608a6b03ed2"),
            {
                { 0x00000000, QByteArray::fromHex("420dbff3"), QByteArray::fromHex("420d3d90") },
                { 0x00047280, QByteArray::fromHex("4cfa003f"), QByteArray::fromHex("7c004ed6") },
            },
        },
    };
}

} // namespace

QVector<RomPatchDefinition> RomPatcher::definitionsForMachine(const QString& machineId)
{
    QVector<RomPatchDefinition> definitions;
    for (const auto& definition : allDefinitions()) {
        if (definition.machineId == machineId) {
            definitions.append(definition);
        }
    }
    return definitions;
}

RomPatchResult RomPatcher::apply(QByteArray& romBytes, const QString& machineId, const QStringList& enabledPatchIds)
{
    return applyDefinitions(romBytes, machineId, enabledPatchIds, definitionsForMachine(machineId));
}

RomPatchResult RomPatcher::applyDefinitions(
    QByteArray& romBytes,
    const QString& machineId,
    const QStringList& enabledPatchIds,
    const QVector<RomPatchDefinition>& definitions)
{
    RomPatchResult result;
    result.originalSha256 = QCryptographicHash::hash(romBytes, QCryptographicHash::Sha256);
    if (enabledPatchIds.isEmpty()) {
        result.success = true;
        return result;
    }

    QVector<RomPatchDefinition> selected;
    for (const auto& patchId : enabledPatchIds) {
        const auto it = std::find_if(definitions.cbegin(), definitions.cend(), [&patchId](const auto& definition) {
            return definition.id == patchId;
        });
        if (it == definitions.cend()) {
            result.error = QStringLiteral("unknown ROM patch '%1' for machine '%2'").arg(patchId, machineId);
            return result;
        }
        if (it->requiredSha256 != result.originalSha256) {
            result.error = QStringLiteral("ROM patch '%1' does not support ROM SHA-256 %2")
                               .arg(patchId, QString::fromLatin1(result.originalSha256.toHex()));
            return result;
        }
        selected.append(*it);
    }

    for (const auto& definition : selected) {
        for (const auto& edit : definition.edits) {
            if (edit.expectedBytes.size() != edit.replacementBytes.size()
                || edit.offset + static_cast<std::uint32_t>(edit.expectedBytes.size()) > static_cast<std::uint32_t>(romBytes.size())
                || romBytes.mid(edit.offset, edit.expectedBytes.size()) != edit.expectedBytes) {
                result.error = QStringLiteral("ROM patch '%1' failed byte verification at offset 0x%2")
                                   .arg(definition.id)
                                   .arg(edit.offset, 0, 16);
                return result;
            }
        }
    }

    auto patchedBytes = romBytes;
    for (const auto& definition : selected) {
        for (const auto& edit : definition.edits) {
            patchedBytes.replace(edit.offset, edit.replacementBytes.size(), edit.replacementBytes);
        }
        result.appliedPatchIds.append(definition.id);
    }
    romBytes = std::move(patchedBytes);
    result.success = true;
    return result;
}

} // namespace cutemac::rom
