#include "cutemac/rom/RomCatalog.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>

#include <algorithm>

#include "cutemac/config/Configuration.h"

namespace cutemac::rom {

RomCatalog::RomCatalog(QString directoryPath)
    : m_directoryPath(directoryPath.isEmpty() ? config::ConfigurationManager::romDirectoryPath() : std::move(directoryPath))
{
    refresh();
}

QVector<RomDefinition> RomCatalog::definitions()
{
    return {
        { QStringLiteral("mac128k-28ba61ce"), QStringLiteral("Macintosh 128K"), RomKind::Machine, QStringLiteral("mac-128k"), QStringLiteral("0x28BA61CE"), 64 * 1024, {}, {}, QByteArray::fromHex("28ba61ce"), 10, false },
        { QStringLiteral("mac512k-28ba4e50"), QStringLiteral("Macintosh 512K"), RomKind::Machine, QStringLiteral("mac-512k"), QStringLiteral("0x28BA4E50"), 64 * 1024, {}, {}, QByteArray::fromHex("28ba4e50"), 10, false },
        { QStringLiteral("mac512ke-v3"), QStringLiteral("Macintosh 512Ke"), RomKind::Machine, QStringLiteral("mac-512ke"), QStringLiteral("v3 / Plus 0x4D1F8172"), 128 * 1024, QByteArray::fromHex("dd908e2b65772a6b1f0c859c24e9a0d3dcde17b1c6a24f4abd8955846d7895e7"), {}, QByteArray::fromHex("4d1f8172"), 30, false },
        { QStringLiteral("mac512ke-v2"), QStringLiteral("Macintosh 512Ke"), RomKind::Machine, QStringLiteral("mac-512ke"), QStringLiteral("v2 / Plus 0x4D1EEAE1"), 128 * 1024, {}, {}, QByteArray::fromHex("4d1eeae1"), 20, true },
        { QStringLiteral("mac512ke-v1"), QStringLiteral("Macintosh 512Ke"), RomKind::Machine, QStringLiteral("mac-512ke"), QStringLiteral("v1 / Plus 0x4D1EEEE1"), 128 * 1024, {}, {}, QByteArray::fromHex("4d1eeee1"), 10, true },
        { QStringLiteral("macplus-v3"), QStringLiteral("Macintosh Plus"), RomKind::Machine, QStringLiteral("mac-plus"), QStringLiteral("v3 (Loud Harmonicas)"), 128 * 1024, QByteArray::fromHex("dd908e2b65772a6b1f0c859c24e9a0d3dcde17b1c6a24f4abd8955846d7895e7"), {}, QByteArray::fromHex("4d1f8172"), 30, false },
        { QStringLiteral("macplus-v2"), QStringLiteral("Macintosh Plus"), RomKind::Machine, QStringLiteral("mac-plus"), QStringLiteral("v2 (Lonely Heifers)"), 128 * 1024, {}, {}, QByteArray::fromHex("4d1eeae1"), 20, true },
        { QStringLiteral("macplus-v1"), QStringLiteral("Macintosh Plus"), RomKind::Machine, QStringLiteral("mac-plus"), QStringLiteral("v1 (Lonely Hearts)"), 128 * 1024, {}, {}, QByteArray::fromHex("4d1eeee1"), 10, true },
        { QStringLiteral("maciicx-97221136"), QStringLiteral("Macintosh IIcx"), RomKind::Machine, QStringLiteral("mac-iicx"), QStringLiteral("0x97221136"), 256 * 1024, QByteArray::fromHex("79fae48e2d5cfde68520e46616503963f8c16430903f410514b62c1379af20cb"), {}, QByteArray::fromHex("97221136"), 10, false },
        { QStringLiteral("quadra800"), QStringLiteral("Macintosh Quadra 800"), RomKind::Machine, QStringLiteral("quadra-800"), {}, 1024 * 1024, QByteArray::fromHex("05ad753fb594e656cf078023ec189e09e2a7655a780de993b75b8c51ed6b09ca"), {}, {}, 10, false },
        { QStringLiteral("powermac-pdm-9feb69b3"), QStringLiteral("Power Macintosh 6100/7100/8100 launch ROM"), RomKind::Machine, QStringLiteral("powermac-8100"), QStringLiteral("9FEB69B3"), 4 * 1024 * 1024, QByteArray::fromHex("9056dd92740d6425e93dcbc8c08c41647f1a14fcb6f5540a5bb2e96b534e8aee"), {}, {}, 10, false },
        { QStringLiteral("apple-m2-video-342-0008-a"), QStringLiteral("Apple Macintosh II Video Card (630-0153)"), RomKind::Device, QStringLiteral("apple_m2_video"), QStringLiteral("342-0008-A"), 4096, {}, QByteArray::fromHex("abe85d8a882bb2b8187a28bd6707fc2f5d77eedd"), {}, 10, false },
        { QStringLiteral("apple-display-card-824-341-0868"), QStringLiteral("Apple Macintosh Display Card 8•24"), RomKind::Device, QStringLiteral("apple_display_card_824"), QStringLiteral("341-0868"), 32 * 1024, QByteArray::fromHex("e2e763a6b432c9196f619a9f90107726ab1a84a1d54242fe5f5182bf3c97b238"), {}, {}, 10, false },
        { QStringLiteral("cutemac-video"), QStringLiteral("CuteMac Video"), RomKind::Embedded, QStringLiteral("cutemac_video"), {}, 4096, {}, {}, {}, 10, false },
    };
}

QString RomCatalog::directoryPath() const { return m_directoryPath; }

void RomCatalog::refresh()
{
    m_statuses.clear();
    for (const auto& definition : definitions()) {
        m_statuses.append({ definition, definition.kind == RomKind::Embedded ? QStringLiteral("(embedded)") : QString(), definition.kind == RomKind::Embedded });
    }

    const QDir directory(m_directoryPath);
    for (const auto& info : directory.entryInfoList(QDir::Files | QDir::Readable, QDir::Name)) {
        QFile file(info.absoluteFilePath());
        if (!file.open(QIODevice::ReadOnly)) continue;
        const auto bytes = file.readAll();
        const auto sha256 = QCryptographicHash::hash(bytes, QCryptographicHash::Sha256);
        const auto sha1 = QCryptographicHash::hash(bytes, QCryptographicHash::Sha1);
        for (auto& status : m_statuses) {
            const auto& definition = status.definition;
            if (status.found || definition.kind == RomKind::Embedded || bytes.size() != definition.expectedSize) continue;
            const bool shaMatch = !definition.sha256.isEmpty() && sha256 == definition.sha256;
            const bool sha1Match = definition.sha256.isEmpty() && !definition.sha1.isEmpty() && sha1 == definition.sha1;
            const bool headerMatch = definition.sha256.isEmpty() && definition.sha1.isEmpty() && !definition.headerChecksum.isEmpty()
                && bytes.startsWith(definition.headerChecksum);
            if (shaMatch || sha1Match || headerMatch) {
                status.found = true;
                status.path = info.absoluteFilePath();
            }
        }
    }
}

QVector<RomStatus> RomCatalog::statuses() const { return m_statuses; }

QString RomCatalog::pathForId(const QString& romId) const
{
    const auto it = std::find_if(m_statuses.cbegin(), m_statuses.cend(), [&](const auto& status) { return status.definition.id == romId && status.found; });
    return it == m_statuses.cend() ? QString() : it->path;
}

QString RomCatalog::preferredMachineRomPath(const QString& machineId) const
{
    const RomStatus* best = nullptr;
    for (const auto& status : m_statuses) {
        if (status.found && status.definition.kind == RomKind::Machine && status.definition.ownerId == machineId
            && (!best || status.definition.preference > best->definition.preference)) best = &status;
    }
    return best ? best->path : QString();
}

QString RomCatalog::deviceRomPath(const QString& deviceId) const
{
    const auto it = std::find_if(m_statuses.cbegin(), m_statuses.cend(), [&](const auto& status) {
        return status.found && status.definition.kind == RomKind::Device && status.definition.ownerId == deviceId;
    });
    return it == m_statuses.cend() ? QString() : it->path;
}

QStringList RomCatalog::requiredRomOwnerIds(const config::Configuration& configuration) const
{
    QStringList result { configuration.machineId };
    for (const auto& device : configuration.nubusDevices) {
        if (device.type == config::NuBusDeviceType::MacintoshIIVideo) result.append(QStringLiteral("apple_m2_video"));
        else if (device.type == config::NuBusDeviceType::AppleDisplayCard824) result.append(QStringLiteral("apple_display_card_824"));
    }
    return result;
}

QStringList RomCatalog::missingRomNames(const config::Configuration& configuration) const
{
    QStringList result;
    for (const auto& owner : requiredRomOwnerIds(configuration)) {
        const bool found = std::any_of(m_statuses.cbegin(), m_statuses.cend(), [&](const auto& status) { return status.found && status.definition.ownerId == owner; });
        if (!found) {
            const auto definition = std::find_if(m_statuses.cbegin(), m_statuses.cend(), [&](const auto& status) { return status.definition.ownerId == owner; });
            result.append(definition == m_statuses.cend() ? owner : definition->definition.displayName);
        }
    }
    result.removeDuplicates();
    return result;
}

QString RomCatalog::warningForConfiguration(const config::Configuration& configuration) const
{
    const auto missing = missingRomNames(configuration);
    if (!missing.isEmpty()) return QStringLiteral("Missing required ROMs:\n\n• %1").arg(missing.join(QStringLiteral("\n• ")));
    const auto selected = preferredMachineRomPath(configuration.machineId);
    const auto it = std::find_if(m_statuses.cbegin(), m_statuses.cend(), [&](const auto& status) { return status.path == selected; });
    if (it != m_statuses.cend() && it->definition.discouraged)
        return QStringLiteral("CuteMac will use %1 %2. This older ROM revision has known bugs; v3 is recommended.").arg(it->definition.displayName, it->definition.revision);
    return {};
}

} // namespace cutemac::rom
