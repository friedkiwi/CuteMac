#include "cutemac/debug/PanicDump.h"

#include <cstdio>

#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLibraryInfo>
#include <QSysInfo>
#include <QStandardPaths>

#include "cutemac/core/IMachine.h"
#include "cutemac/debug/PanicArchive.h"
#include "cutemac/debug/SadMacDetector.h"

#ifndef CUTEMAC_GIT_SHA
#define CUTEMAC_GIT_SHA "unknown"
#endif

namespace cutemac::debug {

namespace {

constexpr const char* manifestMember = "manifest.json";
constexpr const char* snapshotMember = "snapshot.json";
constexpr const char* startupConfigMember = "config-startup.toml";
constexpr const char* runtimeConfigMember = "config-runtime.toml";
constexpr const char* screenMember = "screen.png";
constexpr const char* notesMember = "notes.txt";
constexpr const char* logMember = "log.txt";

QJsonArray toJson(const QStringList& values)
{
    QJsonArray array;
    for (const auto& value : values) array.append(value);
    return array;
}

QStringList fromJsonStringList(const QJsonArray& array)
{
    QStringList values;
    for (const auto value : array) values.append(value.toString());
    return values;
}

QJsonObject toJson(const QMap<QString, QString>& fields)
{
    QJsonObject object;
    for (auto entry = fields.constBegin(); entry != fields.constEnd(); ++entry) {
        object.insert(entry.key(), entry.value());
    }
    return object;
}

QJsonObject toJson(const QMap<QString, std::uint64_t>& values)
{
    QJsonObject object;
    for (auto entry = values.constBegin(); entry != values.constEnd(); ++entry) {
        // As a string: JSON numbers are doubles, and a 64-bit register would
        // quietly lose its low bits on the way back in.
        object.insert(entry.key(), QStringLiteral("0x%1").arg(entry.value(), 0, 16));
    }
    return object;
}

QMap<QString, std::uint64_t> registersFromJson(const QJsonObject& object)
{
    QMap<QString, std::uint64_t> values;
    for (auto entry = object.constBegin(); entry != object.constEnd(); ++entry) {
        auto text = entry.value().toString();
        if (text.startsWith(QStringLiteral("0x"), Qt::CaseInsensitive)) text = text.mid(2);
        values.insert(entry.key(), text.toULongLong(nullptr, 16));
    }
    return values;
}

QJsonObject toJson(const CpuSnapshot& cpu)
{
    QJsonObject object;
    object.insert(QStringLiteral("architecture"), cpu.architecture);
    object.insert(QStringLiteral("pc"), QStringLiteral("0x%1").arg(cpu.pc, 8, 16, QLatin1Char('0')));
    object.insert(QStringLiteral("stack_pointer"),
        QStringLiteral("0x%1").arg(cpu.stackPointer, 8, 16, QLatin1Char('0')));
    object.insert(QStringLiteral("frame_pointer"),
        QStringLiteral("0x%1").arg(cpu.framePointer, 8, 16, QLatin1Char('0')));
    object.insert(QStringLiteral("interrupt_level"), cpu.interruptLevel);
    object.insert(QStringLiteral("halted"), cpu.halted);
    object.insert(QStringLiteral("stopped"), cpu.stopped);
    object.insert(QStringLiteral("register_lines"), toJson(cpu.registerLines));
    object.insert(QStringLiteral("registers"), toJson(cpu.registers));
    object.insert(QStringLiteral("mmu_registers"), toJson(cpu.mmuRegisters));
    object.insert(QStringLiteral("disassembly"), toJson(cpu.disassembly));
    object.insert(QStringLiteral("backtrace"), toJson(cpu.backtrace));
    QJsonArray vectors;
    for (const auto entry : cpu.vectorTable) {
        vectors.append(QStringLiteral("0x%1").arg(entry, 8, 16, QLatin1Char('0')));
    }
    object.insert(QStringLiteral("vector_table"), vectors);
    return object;
}

std::uint32_t parseHex32(const QString& text)
{
    auto value = text;
    if (value.startsWith(QStringLiteral("0x"), Qt::CaseInsensitive)) value = value.mid(2);
    return value.toUInt(nullptr, 16);
}

CpuSnapshot cpuFromJson(const QJsonObject& object)
{
    CpuSnapshot cpu;
    cpu.architecture = object.value(QStringLiteral("architecture")).toString();
    cpu.pc = parseHex32(object.value(QStringLiteral("pc")).toString());
    cpu.stackPointer = parseHex32(object.value(QStringLiteral("stack_pointer")).toString());
    cpu.framePointer = parseHex32(object.value(QStringLiteral("frame_pointer")).toString());
    cpu.interruptLevel = object.value(QStringLiteral("interrupt_level")).toInt();
    cpu.halted = object.value(QStringLiteral("halted")).toBool();
    cpu.stopped = object.value(QStringLiteral("stopped")).toBool();
    cpu.registerLines = fromJsonStringList(object.value(QStringLiteral("register_lines")).toArray());
    cpu.registers = registersFromJson(object.value(QStringLiteral("registers")).toObject());
    cpu.mmuRegisters = registersFromJson(object.value(QStringLiteral("mmu_registers")).toObject());
    cpu.disassembly = fromJsonStringList(object.value(QStringLiteral("disassembly")).toArray());
    cpu.backtrace = fromJsonStringList(object.value(QStringLiteral("backtrace")).toArray());
    for (const auto entry : object.value(QStringLiteral("vector_table")).toArray()) {
        cpu.vectorTable.append(parseHex32(entry.toString()));
    }
    return cpu;
}

QJsonObject toJson(const devices::video::VideoFrame& frame)
{
    QJsonObject object;
    object.insert(QStringLiteral("width"), frame.width);
    object.insert(QStringLiteral("height"), frame.height);
    object.insert(QStringLiteral("stride_bytes"), frame.strideBytes);
    object.insert(QStringLiteral("bits_per_pixel"), frame.bitsPerPixel);
    object.insert(QStringLiteral("storage"),
        frame.storage == devices::video::PixelStorage::Indexed ? QStringLiteral("indexed") : QStringLiteral("direct"));
    object.insert(QStringLiteral("byte_order"),
        frame.byteOrder == devices::video::ByteOrder::BigEndian ? QStringLiteral("big") : QStringLiteral("little"));
    object.insert(QStringLiteral("bit_order"),
        frame.bitOrder == devices::video::BitOrder::MostSignificantFirst ? QStringLiteral("msb")
                                                                        : QStringLiteral("lsb"));
    object.insert(QStringLiteral("grabbable"), frame.grabbable);
    QJsonArray clut;
    for (const auto entry : frame.colorTable) {
        clut.append(QStringLiteral("0x%1").arg(entry, 8, 16, QLatin1Char('0')));
    }
    object.insert(QStringLiteral("color_table"), clut);
    QJsonArray pixelToColor;
    for (const auto entry : frame.pixelToColorIndex) pixelToColor.append(static_cast<int>(entry));
    object.insert(QStringLiteral("pixel_to_color_index"), pixelToColor);
    object.insert(QStringLiteral("pixels_member"), QStringLiteral("frame/pixels.bin"));
    object.insert(QStringLiteral("red_mask"),
        QStringLiteral("0x%1").arg(frame.channels.redMask, 8, 16, QLatin1Char('0')));
    object.insert(QStringLiteral("green_mask"),
        QStringLiteral("0x%1").arg(frame.channels.greenMask, 8, 16, QLatin1Char('0')));
    object.insert(QStringLiteral("blue_mask"),
        QStringLiteral("0x%1").arg(frame.channels.blueMask, 8, 16, QLatin1Char('0')));
    return object;
}

devices::video::VideoFrame frameFromJson(const QJsonObject& object)
{
    devices::video::VideoFrame frame;
    frame.width = object.value(QStringLiteral("width")).toInt();
    frame.height = object.value(QStringLiteral("height")).toInt();
    frame.strideBytes = object.value(QStringLiteral("stride_bytes")).toInt();
    frame.bitsPerPixel = object.value(QStringLiteral("bits_per_pixel")).toInt();
    frame.storage = object.value(QStringLiteral("storage")).toString() == QStringLiteral("direct")
        ? devices::video::PixelStorage::Direct
        : devices::video::PixelStorage::Indexed;
    frame.byteOrder = object.value(QStringLiteral("byte_order")).toString() == QStringLiteral("little")
        ? devices::video::ByteOrder::LittleEndian
        : devices::video::ByteOrder::BigEndian;
    frame.bitOrder = object.value(QStringLiteral("bit_order")).toString() == QStringLiteral("lsb")
        ? devices::video::BitOrder::LeastSignificantFirst
        : devices::video::BitOrder::MostSignificantFirst;
    frame.grabbable = object.value(QStringLiteral("grabbable")).toBool();
    for (const auto entry : object.value(QStringLiteral("color_table")).toArray()) {
        frame.colorTable.append(parseHex32(entry.toString()));
    }
    for (const auto entry : object.value(QStringLiteral("pixel_to_color_index")).toArray()) {
        frame.pixelToColorIndex.append(static_cast<std::uint16_t>(entry.toInt()));
    }
    frame.channels.redMask = parseHex32(object.value(QStringLiteral("red_mask")).toString());
    frame.channels.greenMask = parseHex32(object.value(QStringLiteral("green_mask")).toString());
    frame.channels.blueMask = parseHex32(object.value(QStringLiteral("blue_mask")).toString());
    return frame;
}

QString blobMemberName(const QString& deviceId, const QString& blobName)
{
    auto safeDevice = deviceId;
    safeDevice.replace(QLatin1Char('/'), QLatin1Char('_'));
    return QStringLiteral("devices/%1.%2.bin").arg(safeDevice, blobName);
}

} // namespace

QString panicDumpDirectory()
{
    const auto override = qEnvironmentVariable("CUTEMAC_PANIC_DUMP_DIR");
    if (!override.isEmpty()) return override;
    const auto root = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    return QDir(root).filePath(QStringLiteral("debug_dumps"));
}

QString panicDumpFileName(const QString& machineId, const QDateTime& timestamp)
{
    auto machine = machineId.isEmpty() ? QStringLiteral("unknown") : machineId;
    machine.replace(QLatin1Char('/'), QLatin1Char('-'));
    return QStringLiteral("panic-%1-%2-%3.%4")
        .arg(timestamp.toString(QStringLiteral("yyyyMMdd-HHmmss")), machine,
            QString::fromLatin1(CUTEMAC_GIT_SHA), QString::fromLatin1(panicArchiveExtension));
}

QByteArray serializeSnapshot(const MachineSnapshot& snapshot)
{
    QJsonObject root;
    root.insert(QStringLiteral("schema_version"), snapshot.schemaVersion);
    root.insert(QStringLiteral("machine_id"), snapshot.machineId);
    root.insert(QStringLiteral("cycle"), QString::number(static_cast<qulonglong>(snapshot.cycle)));
    root.insert(QStringLiteral("overlay_enabled"), snapshot.overlayEnabled);
    root.insert(QStringLiteral("rom_loaded"), snapshot.romLoaded);
    root.insert(QStringLiteral("cpu"), toJson(snapshot.cpu));
    root.insert(QStringLiteral("frame"), toJson(snapshot.frame));
    root.insert(QStringLiteral("scheduler_events"), toJson(snapshot.schedulerEvents));
    root.insert(QStringLiteral("notes"), toJson(snapshot.notes));

    QJsonArray memory;
    for (const auto& region : snapshot.memory) {
        QJsonObject entry;
        entry.insert(QStringLiteral("name"), region.name);
        entry.insert(QStringLiteral("kind"), region.kind);
        entry.insert(QStringLiteral("base"), QStringLiteral("0x%1").arg(region.base, 8, 16, QLatin1Char('0')));
        entry.insert(QStringLiteral("length"), QStringLiteral("0x%1").arg(region.length, 8, 16, QLatin1Char('0')));
        if (region.decodeLength != 0 && region.decodeLength != region.length) {
            entry.insert(QStringLiteral("decode_length"),
                QStringLiteral("0x%1").arg(region.decodeLength, 8, 16, QLatin1Char('0')));
        }
        entry.insert(QStringLiteral("readable"), region.readable);
        entry.insert(QStringLiteral("writable"), region.writable);
        entry.insert(QStringLiteral("contents_member"), region.contentsMember);
        memory.append(entry);
    }
    root.insert(QStringLiteral("memory"), memory);

    QJsonArray devices;
    for (const auto& device : snapshot.devices) {
        QJsonObject entry;
        entry.insert(QStringLiteral("id"), device.id);
        entry.insert(QStringLiteral("kind"), device.kind);
        entry.insert(QStringLiteral("state_lines"), toJson(device.stateLines));
        entry.insert(QStringLiteral("fields"), toJson(device.fields));
        QJsonArray blobs;
        for (const auto& blob : device.blobs) {
            QJsonObject blobEntry;
            blobEntry.insert(QStringLiteral("name"), blob.first);
            blobEntry.insert(QStringLiteral("member"), blobMemberName(device.id, blob.first));
            blobEntry.insert(QStringLiteral("size"), blob.second.size());
            blobs.append(blobEntry);
        }
        entry.insert(QStringLiteral("blobs"), blobs);
        devices.append(entry);
    }
    root.insert(QStringLiteral("devices"), devices);

    QJsonObject traces;
    for (auto entry = snapshot.traces.constBegin(); entry != snapshot.traces.constEnd(); ++entry) {
        traces.insert(entry.key(), QStringLiteral("trace/%1.txt").arg(entry.key()));
    }
    root.insert(QStringLiteral("traces"), traces);

    return QJsonDocument(root).toJson(QJsonDocument::Indented);
}

std::optional<MachineSnapshot> deserializeSnapshot(const QByteArray& json)
{
    QJsonParseError parseError {};
    const auto document = QJsonDocument::fromJson(json, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) return std::nullopt;
    const auto root = document.object();

    MachineSnapshot snapshot;
    snapshot.schemaVersion = root.value(QStringLiteral("schema_version")).toInt();
    snapshot.machineId = root.value(QStringLiteral("machine_id")).toString();
    snapshot.cycle = root.value(QStringLiteral("cycle")).toString().toULongLong();
    snapshot.overlayEnabled = root.value(QStringLiteral("overlay_enabled")).toBool();
    snapshot.romLoaded = root.value(QStringLiteral("rom_loaded")).toBool();
    snapshot.cpu = cpuFromJson(root.value(QStringLiteral("cpu")).toObject());
    snapshot.frame = frameFromJson(root.value(QStringLiteral("frame")).toObject());
    snapshot.schedulerEvents = fromJsonStringList(root.value(QStringLiteral("scheduler_events")).toArray());
    snapshot.notes = fromJsonStringList(root.value(QStringLiteral("notes")).toArray());

    for (const auto entry : root.value(QStringLiteral("memory")).toArray()) {
        const auto object = entry.toObject();
        MemoryRegion region;
        region.name = object.value(QStringLiteral("name")).toString();
        region.kind = object.value(QStringLiteral("kind")).toString();
        region.base = parseHex32(object.value(QStringLiteral("base")).toString());
        region.length = parseHex32(object.value(QStringLiteral("length")).toString());
        region.decodeLength = parseHex32(object.value(QStringLiteral("decode_length")).toString());
        region.readable = object.value(QStringLiteral("readable")).toBool();
        region.writable = object.value(QStringLiteral("writable")).toBool();
        region.contentsMember = object.value(QStringLiteral("contents_member")).toString();
        snapshot.memory.append(region);
    }

    for (const auto entry : root.value(QStringLiteral("devices")).toArray()) {
        const auto object = entry.toObject();
        DeviceSnapshot device;
        device.id = object.value(QStringLiteral("id")).toString();
        device.kind = object.value(QStringLiteral("kind")).toString();
        device.stateLines = fromJsonStringList(object.value(QStringLiteral("state_lines")).toArray());
        const auto fields = object.value(QStringLiteral("fields")).toObject();
        for (auto field = fields.constBegin(); field != fields.constEnd(); ++field) {
            device.fields.insert(field.key(), field.value().toString());
        }
        snapshot.devices.append(device);
    }

    return snapshot;
}

PanicDumpResult writePanicDump(const MachineSnapshot& snapshot, const PanicDumpRequest& request)
{
    PanicDumpResult result;
    const auto timestamp = QDateTime::currentDateTime();
    const auto directory = request.directory.isEmpty() ? panicDumpDirectory() : request.directory;

    QDir target(directory);
    if (!target.exists() && !QDir().mkpath(directory)) {
        result.warnings.append(QStringLiteral("cannot create %1").arg(directory));
        return result;
    }
    result.path = target.filePath(panicDumpFileName(snapshot.machineId, timestamp));

    PanicArchiveWriter writer(result.path);
    if (!writer.open()) {
        result.warnings.append(writer.error());
        return result;
    }

    for (const auto& note : snapshot.notes) {
        if (note.startsWith(QStringLiteral("degraded"))) result.degraded = true;
    }

    const auto snapshotJson = serializeSnapshot(snapshot);
    writer.add(QString::fromLatin1(snapshotMember), snapshotJson);

    QVector<QPair<QString, QByteArray>> payload;
    payload.append({ QString::fromLatin1(snapshotMember), snapshotJson });

    for (const auto& region : snapshot.memory) {
        if (region.contentsMember.isEmpty() || region.contents.isEmpty()) continue;
        payload.append({ region.contentsMember, region.contents });
    }
    for (const auto& device : snapshot.devices) {
        for (const auto& blob : device.blobs) {
            payload.append({ blobMemberName(device.id, blob.first), blob.second });
        }
    }
    for (auto entry = snapshot.traces.constBegin(); entry != snapshot.traces.constEnd(); ++entry) {
        payload.append({ QStringLiteral("trace/%1.txt").arg(entry.key()),
            entry.value().join(QLatin1Char('\n')).toUtf8() });
    }
    if (!snapshot.frame.pixels.isEmpty()) {
        payload.append({ QStringLiteral("frame/pixels.bin"), snapshot.frame.pixels });
    }

    config::ConfigurationManager manager;
    if (const auto startup = manager.toTomlBytes(request.startupConfiguration)) {
        payload.append({ QString::fromLatin1(startupConfigMember), *startup });
    } else {
        result.warnings.append(QStringLiteral("startup configuration failed validation; not captured"));
    }
    if (const auto runtime = manager.toTomlBytes(request.runtimeConfiguration)) {
        payload.append({ QString::fromLatin1(runtimeConfigMember), *runtime });
    } else {
        result.warnings.append(QStringLiteral("runtime configuration failed validation; not captured"));
    }

    bool sadMac = false;
    if (snapshot.frame.valid()) {
        sadMac = SadMacDetector::detect(snapshot.frame);
    } else {
        result.warnings.append(QStringLiteral("no valid video frame captured"));
    }
    if (!request.screenshotPng.isEmpty()) {
        payload.append({ QString::fromLatin1(screenMember), request.screenshotPng });
    } else {
        result.warnings.append(QStringLiteral("caller supplied no screenshot"));
    }

    if (!request.note.isEmpty()) {
        payload.append({ QString::fromLatin1(notesMember), request.note.toUtf8() });
    }
    if (!request.hostLog.isEmpty()) {
        payload.append({ QString::fromLatin1(logMember), request.hostLog.join(QLatin1Char('\n')).toUtf8() });
    }

    QJsonObject manifest;
    manifest.insert(QStringLiteral("schema_version"), panicArchiveSchemaVersion);
    manifest.insert(QStringLiteral("snapshot_schema_version"), snapshot.schemaVersion);
    manifest.insert(QStringLiteral("machine_id"), snapshot.machineId);
    manifest.insert(QStringLiteral("cycle"), QString::number(static_cast<qulonglong>(snapshot.cycle)));
    manifest.insert(QStringLiteral("pc"),
        QStringLiteral("0x%1").arg(snapshot.cpu.pc, 8, 16, QLatin1Char('0')));
    manifest.insert(QStringLiteral("timestamp"), timestamp.toString(Qt::ISODate));
    manifest.insert(QStringLiteral("profile_path"), request.profilePath);
    manifest.insert(QStringLiteral("degraded"), result.degraded);
    manifest.insert(QStringLiteral("sad_mac"), sadMac);
    manifest.insert(QStringLiteral("notes"), toJson(snapshot.notes));

    QJsonObject build;
    build.insert(QStringLiteral("git_sha"), QString::fromLatin1(CUTEMAC_GIT_SHA));
    build.insert(QStringLiteral("qt_version"), QString::fromLatin1(qVersion()));
    build.insert(QStringLiteral("qt_build_version"), QLibraryInfo::version().toString());
    build.insert(QStringLiteral("kernel"), QSysInfo::prettyProductName());
    build.insert(QStringLiteral("architecture"), QSysInfo::currentCpuArchitecture());
    build.insert(QStringLiteral("build_abi"), QSysInfo::buildAbi());
    manifest.insert(QStringLiteral("build"), build);

    QJsonArray members;
    for (const auto& [name, contents] : payload) {
        QJsonObject entry;
        entry.insert(QStringLiteral("name"), name);
        entry.insert(QStringLiteral("size"), contents.size());
        entry.insert(QStringLiteral("sha256"),
            QString::fromLatin1(QCryptographicHash::hash(contents, QCryptographicHash::Sha256).toHex()));
        members.append(entry);
    }
    manifest.insert(QStringLiteral("members"), members);

    // Manifest first so a truncated archive still leads with its index.
    writer.add(QString::fromLatin1(manifestMember), QJsonDocument(manifest).toJson(QJsonDocument::Indented));
    for (const auto& [name, contents] : payload) {
        if (name == QString::fromLatin1(snapshotMember)) continue;
        writer.add(name, contents);
    }

    result.memberCount = writer.memberCount();
    if (!writer.close()) {
        result.warnings.append(writer.error());
        return result;
    }

    result.ok = true;
    result.sizeBytes = QFileInfo(result.path).size();
    return result;
}

PanicDumpResult capturePanicDump(core::IDebugMachineAccess& access, const PanicDumpRequest& request,
    std::chrono::milliseconds lockTimeout)
{
    const auto snapshot = access.debugSnapshot(lockTimeout);

    // Announced on stdout rather than through the logging category: when the
    // session is wedged this line is the only confirmation the button worked.
    std::fprintf(stdout, "[panic] triggered machine=%s pc=0x%08x cycle=%llu\n",
        snapshot.machineId.toUtf8().constData(), snapshot.cpu.pc,
        static_cast<unsigned long long>(snapshot.cycle));
    std::fflush(stdout);

    auto result = writePanicDump(snapshot, request);

    if (result.ok) {
        std::fprintf(stdout, "[panic] written %s (%.1f MiB, %d members)\n",
            result.path.toUtf8().constData(),
            static_cast<double>(result.sizeBytes) / (1024.0 * 1024.0), result.memberCount);
    } else {
        std::fprintf(stdout, "[panic] FAILED to write dump to %s\n", result.path.toUtf8().constData());
    }
    for (const auto& warning : result.warnings) {
        std::fprintf(stdout, "[panic] degraded: %s\n", warning.toUtf8().constData());
    }
    for (const auto& note : snapshot.notes) {
        std::fprintf(stdout, "[panic] note: %s\n", note.toUtf8().constData());
    }
    std::fflush(stdout);
    return result;
}

namespace {

// Schema-1 archives recorded a ROM region as one copy of the image, without
// saying that the machine mirrors it across a much wider window. On the II
// family the reset vector and ROMBase both point above that first copy, so the
// captured PC read as open bus and disassembly came back as `dc.w $ffff`.
// Newer dumps carry decodeLength; for older ones, widen a ROM region only when
// its contents would in fact cover an otherwise unreadable PC, and say so.
void widenMirroredRomForCapturedPc(MachineSnapshot& snapshot)
{
    const auto pc = snapshot.cpu.pc;
    const auto covered = [&](const MemoryRegion& region) {
        if (!region.readable || region.contents.isEmpty()) return false;
        const auto window = region.decodeLength != 0 ? region.decodeLength : region.length;
        return pc >= region.base && pc - region.base < window;
    };
    for (const auto& region : snapshot.memory) {
        if (covered(region)) return;
    }
    for (auto& region : snapshot.memory) {
        if (region.kind != QStringLiteral("rom") || region.contents.isEmpty()) continue;
        if (region.decodeLength != 0 || pc < region.base) continue;
        const auto offset = pc - region.base;
        // Only mirroring can explain the PC, and only if the image repeats.
        if (offset % static_cast<std::uint32_t>(region.contents.size()) >= static_cast<std::uint32_t>(region.contents.size())) continue;
        region.decodeLength = offset + static_cast<std::uint32_t>(region.contents.size());
        snapshot.notes.append(QStringLiteral(
            "memory region %1 was recorded without a mirror width; widened to %2 bytes so the "
            "captured pc %3 is readable")
                .arg(region.name)
                .arg(region.decodeLength)
                .arg(QStringLiteral("0x%1").arg(pc, 8, 16, QLatin1Char('0'))));
        return;
    }
}

} // namespace

std::optional<MachineSnapshot> loadPanicDump(const QString& path, QString& error)
{
    PanicArchiveReader reader(path);
    if (!reader.open()) {
        error = reader.error();
        return std::nullopt;
    }
    const auto json = reader.read(QString::fromLatin1(snapshotMember));
    if (!json) {
        error = QStringLiteral("%1 has no %2").arg(path, QString::fromLatin1(snapshotMember));
        return std::nullopt;
    }
    auto snapshot = deserializeSnapshot(*json);
    if (!snapshot) {
        error = QStringLiteral("%1 contains a malformed snapshot").arg(path);
        return std::nullopt;
    }
    if (const auto pixels = reader.read(QStringLiteral("frame/pixels.bin"))) {
        snapshot->frame.pixels = *pixels;
    }
    for (auto& region : snapshot->memory) {
        if (region.contentsMember.isEmpty()) continue;
        if (const auto contents = reader.read(region.contentsMember)) {
            region.contents = *contents;
        } else {
            snapshot->notes.append(
                QStringLiteral("memory region %1 has no contents in the archive").arg(region.name));
        }
    }
    widenMirroredRomForCapturedPc(*snapshot);
    return snapshot;
}

} // namespace cutemac::debug
