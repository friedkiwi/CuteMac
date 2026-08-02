#include <QFile>
#include <QTemporaryDir>

#include <iostream>

#include "cutemac/config/Configuration.h"
#include "cutemac/machines/MachineCatalog.h"

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
    configuration.nvramPath = QStringLiteral("/tmp/mac-plus.nvram");
    configuration.ramSizeKiB = 4096;
    configuration.cyclesPerFrame = 130560;
    configuration.runtimeSpeed = cutemac::config::RuntimeSpeed::Unlimited;
    configuration.iwmDevices.append({ QStringLiteral("/tmp/system.dsk"), true });
    configuration.scsiDevices.append({ 4, cutemac::config::ScsiDeviceType::HardDisk, QStringLiteral("/tmp/disk.hda"), false });
    configuration.nubusDevices.append({ 9, cutemac::config::NuBusDeviceType::CuteMacVideo, {}, 832, 624, 8, 4, true, false });
    configuration.nubusDevices.append({ 11, cutemac::config::NuBusDeviceType::CuteMacVideoAccelerated, {}, 1024, 768, 8, 8, true, true });
    configuration.nubusDevices.append({ 10, cutemac::config::NuBusDeviceType::MacintoshIIVideo, {}, 640, 480, 1, 1, false });
    configuration.serialDevices.append({ 1, cutemac::config::SerialDeviceType::ImageWriterII, QStringLiteral("/tmp/prints") });
    configuration.skipRamPatternTest = true;

    cutemac::config::ConfigurationManager manager;
    ok &= expect(manager.saveTomlFile(path, configuration), "configuration save failed");
    const auto loaded = manager.loadTomlFile(path);
    ok &= expect(loaded.has_value(), "saved TOML must parse");
    if (loaded) {
        ok &= expect(loaded->profileName == configuration.profileName, "quoted profile name did not round-trip");
        ok &= expect(loaded->romPath.isEmpty(), "new profiles must not store a per-machine ROM path");
        ok &= expect(loaded->nvramPath == configuration.nvramPath, "NVRAM path did not round-trip");
        ok &= expect(loaded->skipRamPatternTest, "ROM patch setting did not round-trip");
        ok &= expect(loaded->runtimeSpeed == cutemac::config::RuntimeSpeed::Unlimited, "runtime speed did not round-trip");
        ok &= expect(loaded->iwmDevices.size() == 1 && loaded->iwmDevices.first().readOnly, "IWM device did not round-trip");
        ok &= expect(loaded->scsiDevices.size() == 1 && loaded->scsiDevices.first().id == 4, "SCSI device did not round-trip");
        ok &= expect(loaded->nubusDevices.size() == 3 && loaded->nubusDevices.first().width == 832
                && !loaded->nubusDevices.first().absolutePointer
                && loaded->nubusDevices[1].type == cutemac::config::NuBusDeviceType::CuteMacVideoAccelerated
                && loaded->nubusDevices.last().type == cutemac::config::NuBusDeviceType::MacintoshIIVideo,
            "NuBus devices did not round-trip");
        ok &= expect(loaded->serialDevices.size() == 1 && loaded->serialDevices.first().channel == 1
                && loaded->serialDevices.first().outputDirectory == QStringLiteral("/tmp/prints"),
            "serial printer did not round-trip");
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

    QFile powerMacProfile(path);
    ok &= expect(powerMacProfile.open(QIODevice::WriteOnly | QIODevice::Truncate), "Power Macintosh migration fixture open failed");
    powerMacProfile.write("name = \"Power Macintosh\"\n[machine]\nid = \"powermac-6100\"\n");
    powerMacProfile.close();
    const auto migratedPowerMac = manager.loadTomlFile(path);
    ok &= expect(migratedPowerMac.has_value() && migratedPowerMac->machineId == QStringLiteral("powermac-8100"),
        "legacy Power Macintosh 6100 target must migrate to the 8100 target");
    ok &= expect(migratedPowerMac && migratedPowerMac->ramSizeKiB == 8192,
        "a legacy Power Macintosh profile must receive a valid default RAM size");

    QFile invalidRam(path);
    ok &= expect(invalidRam.open(QIODevice::WriteOnly | QIODevice::Truncate), "invalid RAM fixture open failed");
    invalidRam.write("name = \"Invalid Plus\"\n[machine]\nid = \"mac-plus\"\nram_size_mib = 3\n");
    invalidRam.close();
    ok &= expect(!manager.loadTomlFile(path).has_value(), "unsupported RAM size in TOML must be rejected");

    QFile validFractionalRam(path);
    ok &= expect(validFractionalRam.open(QIODevice::WriteOnly | QIODevice::Truncate), "fractional RAM fixture open failed");
    validFractionalRam.write("name = \"2.5 MiB Plus\"\n[machine]\nid = \"mac-plus\"\nram_size_kib = 2560\n");
    validFractionalRam.close();
    const auto fractionalPlus = manager.loadTomlFile(path);
    ok &= expect(fractionalPlus && fractionalPlus->ramSizeKiB == 2560,
        "the Macintosh Plus 2.5 MiB configuration must be supported");

    auto invalidConfiguration = configuration;
    invalidConfiguration.ramSizeKiB = 3072;
    ok &= expect(!manager.saveTomlFile(path, invalidConfiguration), "saving unsupported RAM must fail");

    ok &= expect(cutemac::machines::MachineCatalog::isValidRamSize(QStringLiteral("mac-iicx"), 20480),
        "catalog must accept a valid IIcx bank configuration");
    ok &= expect(!cutemac::machines::MachineCatalog::isValidRamSize(QStringLiteral("mac-iicx"), 24576),
        "catalog must reject an invalid IIcx bank configuration");
    const auto iicx = cutemac::machines::MachineCatalog::find(QStringLiteral("mac-iicx"));
    const QVector<int> validIIcxRamSizes {
        1024, 2048, 4096, 5120, 8192, 16384, 17408, 20480,
        32768, 65536, 66560, 69632, 81920, 131072,
    };
    ok &= expect(iicx && iicx->supportedRamSizesKiB == validIIcxRamSizes,
        "IIcx RAM combo must contain only complete four-SIMM bank configurations");

    QFile malformed(path);
    ok &= expect(malformed.open(QIODevice::WriteOnly | QIODevice::Truncate), "malformed fixture open failed");
    malformed.write("[machine\nid =");
    malformed.close();
    ok &= expect(!manager.loadTomlFile(path).has_value(), "malformed TOML must be rejected");

    return ok ? 0 : 1;
}
