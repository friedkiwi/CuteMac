#include <QFile>
#include <QTemporaryDir>

#include <iostream>

#include "cutemac/config/Configuration.h"

namespace {

bool expect(bool condition, const char* message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
    }
    return condition;
}

} // namespace

int main()
{
    bool ok = true;
    QTemporaryDir directory;
    const auto path = directory.filePath(QStringLiteral("profile.toml"));

    cutemac::config::Configuration configuration;
    configuration.profileName = QStringLiteral("Quoted \"Plus\"");
    configuration.machineId = QStringLiteral("mac-plus");
    configuration.romPath = QStringLiteral("/tmp/a \\ b.rom");
    configuration.ramSizeMiB = 4;
    configuration.cyclesPerFrame = 130560;
    configuration.runtimeSpeed = cutemac::config::RuntimeSpeed::Unlimited;
    configuration.iwmDevices.append({ QStringLiteral("/tmp/system.dsk"), true });
    configuration.scsiDevices.append({ 4, cutemac::config::ScsiDeviceType::HardDisk, QStringLiteral("/tmp/disk.hda"), false });
    configuration.skipRamPatternTest = true;

    cutemac::config::ConfigurationManager manager;
    ok &= expect(manager.saveTomlFile(path, configuration), "configuration save failed");
    const auto loaded = manager.loadTomlFile(path);
    ok &= expect(loaded.has_value(), "saved TOML must parse");
    if (loaded) {
        ok &= expect(loaded->profileName == configuration.profileName, "quoted profile name did not round-trip");
        ok &= expect(loaded->romPath == configuration.romPath, "escaped path did not round-trip");
        ok &= expect(loaded->skipRamPatternTest, "ROM patch setting did not round-trip");
        ok &= expect(loaded->runtimeSpeed == cutemac::config::RuntimeSpeed::Unlimited, "runtime speed did not round-trip");
        ok &= expect(loaded->iwmDevices.size() == 1 && loaded->iwmDevices.first().readOnly, "IWM device did not round-trip");
        ok &= expect(loaded->scsiDevices.size() == 1 && loaded->scsiDevices.first().id == 4, "SCSI device did not round-trip");
        ok &= expect(loaded->enabledRomPatches() == QStringList { QStringLiteral("macplus.skip_ram_pattern_test") },
            "enabled ROM patch ID is incorrect");
    }

    QFile legacy(path);
    ok &= expect(legacy.open(QIODevice::WriteOnly | QIODevice::Truncate), "legacy fixture open failed");
    legacy.write("name = \"Legacy\"\n[machine]\nid = \"mac-plus\"\n[storage]\ndisk_path = \"old.hda\"\nfloppy_path = \"old.dsk\"\n");
    legacy.close();
    const auto migrated = manager.loadTomlFile(path);
    ok &= expect(migrated.has_value(), "legacy profile must parse");
    if (migrated) {
        ok &= expect(migrated->runtimeSpeed == cutemac::config::RuntimeSpeed::Unlimited, "legacy profile must default to unlimited");
        ok &= expect(migrated->iwmDevices.size() == 1 && migrated->iwmDevices.first().imagePath == QStringLiteral("old.dsk"), "legacy floppy was not migrated");
        ok &= expect(migrated->scsiDevices.size() == 1 && migrated->scsiDevices.first().imagePath == QStringLiteral("old.hda"), "legacy disk was not migrated");
    }

    QFile malformed(path);
    ok &= expect(malformed.open(QIODevice::WriteOnly | QIODevice::Truncate), "malformed fixture open failed");
    malformed.write("[machine\nid =");
    malformed.close();
    ok &= expect(!manager.loadTomlFile(path).has_value(), "malformed TOML must be rejected");

    return ok ? 0 : 1;
}
