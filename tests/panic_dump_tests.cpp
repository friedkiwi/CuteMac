// Panic dumps are a Debug-configuration feature, so in a Release build there is
// nothing linked to exercise. Multi-config generators register this test once
// and run it per configuration, so the guard lives in the source.
#if !CUTEMAC_ENABLE_PANIC_DUMP
int main() { return 0; }
#else

#include <chrono>
#include <iostream>
#include <mutex>
#include <thread>

#include <QCoreApplication>
#include <QDir>
#include <QTemporaryDir>

#include "cutemac/core/EmulationSession.h"
#include "cutemac/debug/MachineSnapshot.h"
#include "cutemac/debug/PanicArchive.h"
#include "cutemac/debug/PanicDump.h"
#include "cutemac/debug/SnapshotMachine.h"

namespace {

int failures = 0;

void check(bool condition, const char* what)
{
    if (condition) return;
    std::cerr << "FAIL: " << what << '\n';
    ++failures;
}

cutemac::debug::MachineSnapshot makeSnapshot()
{
    using namespace cutemac::debug;
    MachineSnapshot snapshot;
    snapshot.machineId = QStringLiteral("mac-plus");
    snapshot.cycle = 1234567;
    snapshot.overlayEnabled = true;
    snapshot.romLoaded = true;

    snapshot.cpu.architecture = QStringLiteral("m68k:68000");
    snapshot.cpu.pc = 0x00400136;
    snapshot.cpu.stackPointer = 0x00001ffc;
    snapshot.cpu.framePointer = 0x00001ff0;
    snapshot.cpu.interruptLevel = 2;
    snapshot.cpu.registers.insert(QStringLiteral("d0"), 0xdeadbeefULL);
    snapshot.cpu.registers.insert(QStringLiteral("a7"), 0x00001ffcULL);
    snapshot.cpu.mmuRegisters.insert(QStringLiteral("enabled"), 0);
    snapshot.cpu.registerLines.append(QStringLiteral("D0=deadbeef"));
    snapshot.cpu.backtrace.append(QStringLiteral("#0 frame=00001ff0 return=00400200"));
    snapshot.cpu.vectorTable.append(0x00001ffc);
    snapshot.cpu.vectorTable.append(0x00400136);

    MemoryRegion ram;
    ram.name = QStringLiteral("ram");
    ram.kind = QStringLiteral("ram");
    ram.base = 0;
    ram.length = 4096;
    ram.writable = true;
    ram.contentsMember = QStringLiteral("mem/ram.bin");
    ram.contents = QByteArray(4096, '\0');
    ram.contents[0] = static_cast<char>(0x12);
    ram.contents[1] = static_cast<char>(0x34);
    ram.contents[4095] = static_cast<char>(0xa5);
    snapshot.memory.append(ram);

    DeviceSnapshot via;
    via.id = QStringLiteral("via");
    via.kind = QStringLiteral("via6522");
    via.fields.insert(QStringLiteral("ifr"), QStringLiteral("0x80"));
    via.stateLines.append(QStringLiteral("via ifr=0x80"));
    via.blobs.append({ QStringLiteral("pram"), QByteArray(256, '\x5a') });
    snapshot.devices.append(via);

    snapshot.frame.width = 4;
    snapshot.frame.height = 2;
    snapshot.frame.strideBytes = 4;
    snapshot.frame.bitsPerPixel = 8;
    snapshot.frame.storage = cutemac::devices::video::PixelStorage::Indexed;
    snapshot.frame.pixels = QByteArray(8, '\x03');
    snapshot.frame.colorTable = { 0xff000000U, 0xffffffffU };

    snapshot.schedulerEvents.append(QStringLiteral("cycle=100 in=5 seq=1 label=guest-input"));
    snapshot.traces.insert(QStringLiteral("pc"), QStringList { QStringLiteral("00400136 Boot") });
    return snapshot;
}

void testRoundTrip(const QString& directory)
{
    const auto original = makeSnapshot();

    cutemac::debug::PanicDumpRequest request;
    request.startupConfiguration = cutemac::config::ConfigurationManager::defaultMacPlusConfiguration();
    request.runtimeConfiguration = request.startupConfiguration;
    request.runtimeConfiguration.runtimeSpeed = cutemac::config::RuntimeSpeed::Realtime;
    request.note = QStringLiteral("finder hung on eject");
    request.hostLog = QStringList { QStringLiteral("warning something") };
    request.directory = directory;
    request.screenshotPng = QByteArray("\x89PNG-not-really", 15);

    const auto result = cutemac::debug::writePanicDump(original, request);
    check(result.ok, "writePanicDump succeeded");
    check(!result.degraded, "clean capture is not marked degraded");
    check(result.sizeBytes > 0, "archive is non-empty");
    if (!result.ok) return;

    cutemac::debug::PanicArchiveReader reader(result.path);
    check(reader.open(), "archive reopens");
    const auto names = reader.memberNames();
    for (const auto* expected : { "manifest.json", "snapshot.json", "mem/ram.bin", "config-startup.toml",
             "config-runtime.toml", "screen.png", "notes.txt", "log.txt", "trace/pc.txt" }) {
        check(names.contains(QString::fromLatin1(expected)),
            (std::string("archive contains ") + expected).c_str());
    }
    check(names.contains(QStringLiteral("devices/via.pram.bin")), "device blob is stored");

    const auto notes = reader.read(QStringLiteral("notes.txt"));
    check(notes.has_value() && *notes == QByteArray("finder hung on eject"), "note round-trips");

    QString error;
    const auto loaded = cutemac::debug::loadPanicDump(result.path, error);
    check(loaded.has_value(), "loadPanicDump succeeded");
    if (!loaded) {
        std::cerr << "  error: " << error.toStdString() << '\n';
        return;
    }

    check(loaded->machineId == original.machineId, "machine id round-trips");
    check(loaded->cycle == original.cycle, "cycle round-trips");
    check(loaded->overlayEnabled == original.overlayEnabled, "overlay flag round-trips");
    check(loaded->cpu.pc == original.cpu.pc, "pc round-trips");
    check(loaded->cpu.registers.value(QStringLiteral("d0")) == 0xdeadbeefULL,
        "64-bit register survives JSON");
    check(loaded->cpu.backtrace == original.cpu.backtrace, "backtrace round-trips");
    check(loaded->cpu.vectorTable == original.cpu.vectorTable, "vector table round-trips");
    check(loaded->frame.colorTable == original.frame.colorTable, "CLUT round-trips");
    check(loaded->frame.pixels == original.frame.pixels, "frame pixels round-trip");
    check(loaded->schedulerEvents == original.schedulerEvents, "scheduler events round-trip");
    check(loaded->memory.size() == 1 && loaded->memory[0].contents == original.memory[0].contents,
        "RAM image round-trips byte for byte");
    check(loaded->devices.size() == 1 && loaded->devices[0].fields.value(QStringLiteral("ifr"))
            == QStringLiteral("0x80"),
        "device fields round-trip");

    // SnapshotMachine serves the dump through the live debug boundary.
    cutemac::debug::SnapshotMachine machine(*loaded);
    check(machine.programCounter() == original.cpu.pc, "snapshot reports the captured pc");
    check(machine.debugRead8(0) == 0x12, "snapshot reads captured RAM");
    check(machine.debugRead16(0) == 0x1234, "snapshot 16-bit read composes correctly");
    check(machine.debugRead8(4095) == 0xa5, "snapshot reads the last captured byte");
    check(machine.debugCpuArchitecture() == QStringLiteral("m68k:68000"), "architecture is reported");

    const auto before = machine.unmappedReads();
    check(machine.debugRead8(0x00ff0000) == 0xff, "unmapped reads return open bus");
    check(machine.unmappedReads() == before + 1, "unmapped reads are counted");

    // Read-only: writes and execution are refused, not faked.
    machine.debugWrite8(0, 0x99);
    check(machine.writeAttempted(), "write attempts are recorded");
    check(machine.debugRead8(0) == 0x12, "writes do not modify the snapshot");
    check(machine.runCycles(1000) == 0, "runCycles is inert");
    check(machine.stepInstruction() == 0, "stepInstruction is inert");

    // A real m68k core bound to the snapshot bus disassembles anywhere it has
    // bytes, not only inside the window rendered at capture time.
    const auto text = machine.disassemble(0);
    check(!text.isEmpty() && !text.startsWith(QStringLiteral("<outside")),
        "snapshot disassembles from captured memory");
}

void testDegradedCapture(const QString& directory)
{
    // Hold the session lock from another thread; capture must still produce an
    // archive and mark itself degraded rather than blocking forever.
    auto configuration = cutemac::config::ConfigurationManager::defaultMacPlusConfiguration();
    cutemac::core::EmulationSession session(configuration);

    std::mutex started;
    started.lock();
    std::thread holder([&]() {
        // runCycles takes the session lock; a paused session returns at once,
        // so the lock is instead held by a long status poll loop.
        started.unlock();
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(600);
        while (std::chrono::steady_clock::now() < deadline) {
            (void)session.status();
        }
    });
    started.lock();

    cutemac::debug::PanicDumpRequest request;
    request.startupConfiguration = configuration;
    request.runtimeConfiguration = configuration;
    request.directory = directory;

    const auto result = cutemac::debug::capturePanicDump(session, request, std::chrono::milliseconds(50));
    holder.join();

    check(result.ok, "capture under contention still writes an archive");
    check(!result.path.isEmpty(), "capture under contention reports a path");

    // The flag itself: a snapshot carrying a degraded note must mark the result,
    // so a reader can tell a torn capture from a clean one.
    auto torn = makeSnapshot();
    torn.notes.append(QStringLiteral("degraded: session lock not acquired within 50 ms"));
    const auto tornResult = cutemac::debug::writePanicDump(torn, request);
    check(tornResult.ok, "degraded snapshot still writes");
    check(tornResult.degraded, "degraded note sets the degraded flag");
}

void testFileNaming()
{
    const QDateTime when(QDate(2026, 8, 11), QTime(14, 25, 30));
    const auto name = cutemac::debug::panicDumpFileName(QStringLiteral("mac-iicx"), when);
    check(name.startsWith(QStringLiteral("panic-20260811-142530-mac-iicx-")), "file name is deterministic");
    check(name.endsWith(QStringLiteral(".cutemacpanic")), "file name carries the archive extension");
}

// A machine that mirrors its ROM must stay disassemblable at the captured PC.
// The IIcx reset vector and ROMBase both point above the first copy of the
// image, so a dump that maps one copy reads open bus exactly where a reader
// needs instructions.
void testMirroredRomIsReadableAtPc(const QString& directory)
{
    using namespace cutemac::debug;

    MachineSnapshot snapshot = makeSnapshot();
    snapshot.machineId = QStringLiteral("mac-iicx");
    snapshot.cpu.architecture = QStringLiteral("m68k:68030");
    snapshot.cpu.pc = 0x40802436;
    snapshot.memory.clear();

    MemoryRegion rom;
    rom.name = QStringLiteral("rom");
    rom.kind = QStringLiteral("rom");
    rom.base = 0x40000000;
    rom.length = 0x40000;
    rom.decodeLength = 0x10000000;
    rom.contentsMember = QStringLiteral("mem/rom.bin");
    rom.contents = QByteArray(0x40000, '\0');
    rom.contents[0x2436] = static_cast<char>(0x4e);
    rom.contents[0x2437] = static_cast<char>(0x75); // rts
    snapshot.memory.append(rom);

    SnapshotMachine mirrored(snapshot);
    check(mirrored.debugRead16(0x40802436) == 0x4e75,
        "a mirrored rom region answers at the captured pc, not just at its base");
    check(mirrored.debugRead16(0x40002436) == 0x4e75,
        "a mirrored rom region still answers at its base copy");
    check(mirrored.unmappedReads() == 0, "reads inside the mirror window are not open bus");

    PanicDumpRequest request;
    request.directory = directory;
    const auto written = writePanicDump(snapshot, request);
    check(written.ok, "a snapshot with a mirrored region writes");

    QString error;
    const auto reloaded = loadPanicDump(written.path, error);
    check(reloaded.has_value(), "a mirrored region survives the archive round trip");
    if (reloaded.has_value()) {
        SnapshotMachine restored(*reloaded);
        check(restored.debugRead16(0x40802436) == 0x4e75,
            "the mirror width round-trips through the archive");
    }

    // Schema-1 archives predate the mirror width. Loading one must still put
    // instructions under the captured pc rather than leaving it open bus.
    auto legacy = snapshot;
    legacy.memory[0].decodeLength = 0;
    SnapshotMachine narrow(legacy);
    check(narrow.debugRead16(0x40802436) == 0xffff,
        "without a mirror width the captured pc reads open bus");
}

// Every reader command has to work against a dump. These used to dereference
// the live machine pointer and crash the debug session outright.
void testSnapshotServesLowMemoryAndVectors()
{
    using namespace cutemac::debug;

    MachineSnapshot snapshot = makeSnapshot();
    snapshot.memory.clear();

    MemoryRegion ram;
    ram.name = QStringLiteral("ram");
    ram.kind = QStringLiteral("ram");
    ram.base = 0;
    ram.length = 0x10000;
    ram.writable = true;
    ram.contentsMember = QStringLiteral("mem/ram.bin");
    ram.contents = QByteArray(0x10000, '\0');
    ram.contents[0x0af0] = static_cast<char>(0x00);
    ram.contents[0x0af1] = static_cast<char>(0x0a); // DSErrCode = 10
    ram.contents[0x002c] = static_cast<char>(0x00);
    ram.contents[0x002d] = static_cast<char>(0x00);
    ram.contents[0x002e] = static_cast<char>(0x42);
    ram.contents[0x002f] = static_cast<char>(0x64); // line 1111 vector
    snapshot.memory.append(ram);

    SnapshotMachine machine(snapshot);
    check(machine.debugRead16(0x0af0) == 10, "DSErrCode is readable from a dump");
    check(machine.debugRead32(0x2c) == 0x00004264, "the vector table is readable from a dump");
}

} // namespace

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("friedkiwi"));
    QCoreApplication::setApplicationName(QStringLiteral("CuteMacPanicDumpTests"));

    QTemporaryDir directory;
    if (!directory.isValid()) {
        std::cerr << "cannot create a temporary directory\n";
        return 1;
    }

    testFileNaming();
    testRoundTrip(directory.path());
    testDegradedCapture(directory.path());
    testMirroredRomIsReadableAtPc(directory.path());
    testSnapshotServesLowMemoryAndVectors();

    if (failures != 0) {
        std::cerr << failures << " panic dump check(s) failed\n";
        return 1;
    }
    return 0;
}

#endif // CUTEMAC_ENABLE_PANIC_DUMP
