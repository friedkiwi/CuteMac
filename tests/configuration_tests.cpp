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
    configuration.iwmDevices.append({ QStringLiteral("/tmp/external.dsk"), false });
    configuration.scsiDevices.append({ 4, cutemac::config::ScsiDeviceType::HardDisk, QStringLiteral("/tmp/disk.hda"), false });
    configuration.nubusDevices.append({ 9, cutemac::config::NuBusDeviceType::CuteMacVideo, {}, 832, 624, 8, 4096, true, false });
    configuration.nubusDevices.append({ 11, cutemac::config::NuBusDeviceType::CuteMacVideoAccelerated, {}, 1024, 768, 8, 8192, true, true });
    configuration.nubusDevices.append({ 10, cutemac::config::NuBusDeviceType::MacintoshIIVideo, {}, 640, 480, 1, 512, false });
    configuration.nubusDevices.append({ 12, cutemac::config::NuBusDeviceType::AppleDisplayCard824, {}, 640, 480, 8, 1024, false });
    configuration.serialDevices.append({ 1, cutemac::config::SerialDeviceType::ImageWriterII, QStringLiteral("/tmp/prints") });
    cutemac::config::SerialDeviceConfiguration modem;
    modem.channel = 0;
    modem.type = cutemac::config::SerialDeviceType::HayesModem;
    modem.directTcpDialing = true;
    modem.phonebook = cutemac::config::defaultSerialModemPhonebook();
    modem.phonebook.append({ QStringLiteral("5551212"), QStringLiteral("tcp:bbs.example.org:23"), true });
    configuration.serialDevices.append(modem);
    cutemac::config::SerialDeviceConfiguration nullModem;
    nullModem.channel = 0;
    nullModem.type = cutemac::config::SerialDeviceType::NullModem;
    nullModem.tcpMode = cutemac::config::SerialTcpMode::Dial;
    nullModem.tcpHost = QStringLiteral("debug.example.org");
    nullModem.tcpPort = 2323;
    configuration.serialDevices.append(nullModem);
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
        ok &= expect(loaded->iwmDevices.size() == 2 && loaded->iwmDevices.first().readOnly
                && loaded->iwmDevices[1].imagePath == QStringLiteral("/tmp/external.dsk"),
            "IWM devices did not round-trip");
        ok &= expect(loaded->scsiDevices.size() == 1 && loaded->scsiDevices.first().id == 4, "SCSI device did not round-trip");
        ok &= expect(loaded->nubusDevices.size() == 4 && loaded->nubusDevices.first().width == 832
                && loaded->nubusDevices.first().vramKiB == 4096
                && !loaded->nubusDevices.first().absolutePointer
                && loaded->nubusDevices[1].type == cutemac::config::NuBusDeviceType::CuteMacVideoAccelerated
                && loaded->nubusDevices[1].vramKiB == 8192
                && loaded->nubusDevices[2].type == cutemac::config::NuBusDeviceType::MacintoshIIVideo
                && loaded->nubusDevices.last().type == cutemac::config::NuBusDeviceType::AppleDisplayCard824
                && loaded->nubusDevices.last().vramKiB == 1024,
            "NuBus devices did not round-trip");
        ok &= expect(loaded->serialDevices.size() == 3 && loaded->serialDevices.first().channel == 1
                && loaded->serialDevices.first().outputDirectory == QStringLiteral("/tmp/prints")
                && loaded->serialDevices[1].type == cutemac::config::SerialDeviceType::HayesModem
                && loaded->serialDevices[1].directTcpDialing
                && loaded->serialDevices[1].phonebook.size() == 3
                && loaded->serialDevices[1].phonebook.first().number == QStringLiteral("1000")
                && loaded->serialDevices[1].phonebook.first().target == QStringLiteral("slip:libslirp")
                && loaded->serialDevices[1].phonebook[1].number == QStringLiteral("1001")
                && loaded->serialDevices[1].phonebook[1].target == QStringLiteral("ppp:libslirp")
                && loaded->serialDevices[1].phonebook[2].telnet
                && loaded->serialDevices[2].type == cutemac::config::SerialDeviceType::NullModem
                && loaded->serialDevices[2].tcpMode == cutemac::config::SerialTcpMode::Dial
                && loaded->serialDevices[2].tcpHost == QStringLiteral("debug.example.org")
                && loaded->serialDevices[2].tcpPort == 2323,
            "serial devices did not round-trip");
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
        ok &= expect(migrated->iwmDevices.size() == 2 && migrated->iwmDevices.first().imagePath == QStringLiteral("old.dsk")
                && migrated->iwmDevices[1].imagePath.isEmpty(),
            "legacy floppy was not migrated");
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

    QFile compactProfile(path);
    ok &= expect(compactProfile.open(QIODevice::WriteOnly | QIODevice::Truncate), "compact profile fixture open failed");
    compactProfile.write("name = \"Mac 128K\"\n[machine]\nid = \"mac-128k\"\n");
    compactProfile.close();
    const auto compact128k = manager.loadTomlFile(path);
    ok &= expect(compact128k && compact128k->machineId == QStringLiteral("mac-128k")
            && compact128k->ramSizeKiB == 128,
        "Macintosh 128K profile must default to its fixed RAM size");

    QFile oversizedVideo(path);
    ok &= expect(oversizedVideo.open(QIODevice::WriteOnly | QIODevice::Truncate), "oversized video fixture open failed");
    oversizedVideo.write("name = \"Oversized Video\"\n[machine]\nid = \"mac-iicx\"\nram_size_kib = 16384\n"
                         "[[nubus.devices]]\nslot = 9\ntype = \"cutemac_video\"\nwidth = 1152\nheight = 870\ndepth = 8\nvram_mib = 4\n");
    oversizedVideo.close();
    ok &= expect(!manager.loadTomlFile(path).has_value(),
        "CuteMac Video profiles must reject framebuffers that overlap MMIO while migrating legacy vram_mib");

    ok &= expect(cutemac::config::isValidNuBusDeviceConfiguration(
                     { 9, cutemac::config::NuBusDeviceType::CuteMacVideo, {}, 1024, 768, 8, 4096, true, true }),
        "CuteMac Video must accept the largest current 1024x768 eight-bit profile");
    ok &= expect(!cutemac::config::isValidNuBusDeviceConfiguration(
                     { 9, cutemac::config::NuBusDeviceType::CuteMacVideo, {}, 1024, 768, 16, 4096, true, true }),
        "CuteMac Video must reject profiles whose maximum depth exceeds the safe standard-slot aperture");
    ok &= expect(!cutemac::config::isValidNuBusDeviceConfiguration(
                     { 9, cutemac::config::NuBusDeviceType::CuteMacVideo, {}, 640, 480, 8, 512, true, true }),
        "CuteMac Video must reject sub-MiB VRAM even though authentic Apple cards may use it");
    ok &= expect(cutemac::config::isValidNuBusDeviceConfiguration(
                     { 9, cutemac::config::NuBusDeviceType::AppleDisplayCard824, {}, 640, 480, 8, 512, false, true })
            && cutemac::config::isValidNuBusDeviceConfiguration(
                { 9, cutemac::config::NuBusDeviceType::AppleDisplayCard824, {}, 640, 480, 8, 1024, false, true })
            && !cutemac::config::isValidNuBusDeviceConfiguration(
                { 9, cutemac::config::NuBusDeviceType::AppleDisplayCard824, {}, 640, 480, 8, 4096, false, true }),
        "Apple Display Card 8-24 must accept only authentic 512 KiB and 1 MiB VRAM sizes");

    auto invalidConfiguration = configuration;
    invalidConfiguration.ramSizeKiB = 3072;
    ok &= expect(!manager.saveTomlFile(path, invalidConfiguration), "saving unsupported RAM must fail");
    auto invalidVideoConfiguration = configuration;
    invalidVideoConfiguration.nubusDevices[1].width = 1152;
    invalidVideoConfiguration.nubusDevices[1].height = 870;
    invalidVideoConfiguration.nubusDevices[1].depth = 8;
    ok &= expect(!manager.saveTomlFile(path, invalidVideoConfiguration),
        "saving oversized CuteMac Video geometry must fail");
    auto invalidModemConfiguration = configuration;
    invalidModemConfiguration.serialDevices[1].slip.enabled = false;
    ok &= expect(!manager.saveTomlFile(path, invalidModemConfiguration),
        "saving a SLIP phonebook target with SLIP disabled must fail");
    auto invalidNullModemConfiguration = configuration;
    invalidNullModemConfiguration.serialDevices[2].tcpPort = 0;
    ok &= expect(!manager.saveTomlFile(path, invalidNullModemConfiguration),
        "saving a null modem without a TCP port must fail");

    QFile legacyModem(path);
    ok &= expect(legacyModem.open(QIODevice::WriteOnly | QIODevice::Truncate), "legacy modem fixture open failed");
    legacyModem.write("name = \"Modem\"\n[machine]\nid = \"mac-plus\"\n[[serial.devices]]\nchannel = 0\ntype = \"hayes_modem\"\n");
    legacyModem.close();
    const auto loadedModem = manager.loadTomlFile(path);
    ok &= expect(loadedModem && loadedModem->serialDevices.size() == 1
            && loadedModem->serialDevices.first().phonebook.size() == 2
            && loadedModem->serialDevices.first().phonebook.first().number == QStringLiteral("1000")
            && loadedModem->serialDevices.first().phonebook.first().target == QStringLiteral("slip:libslirp")
            && loadedModem->serialDevices.first().phonebook[1].number == QStringLiteral("1001")
            && loadedModem->serialDevices.first().phonebook[1].target == QStringLiteral("ppp:libslirp"),
        "Hayes modem must default phone numbers 1000/1001 to SLIP/PPP");

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
    ok &= expect(cutemac::machines::MachineCatalog::isValidRamSize(QStringLiteral("mac-128k"), 128),
        "catalog must accept the Macintosh 128K fixed RAM size");
    ok &= expect(cutemac::machines::MachineCatalog::isValidRamSize(QStringLiteral("mac-512k"), 512),
        "catalog must accept the Macintosh 512K fixed RAM size");
    ok &= expect(cutemac::machines::MachineCatalog::isValidRamSize(QStringLiteral("mac-512ke"), 512),
        "catalog must accept the Macintosh 512Ke fixed RAM size");
    ok &= expect(!cutemac::machines::MachineCatalog::isValidRamSize(QStringLiteral("mac-512k"), 1024),
        "catalog must reject invalid compact Macintosh RAM sizes");

    QFile malformed(path);
    ok &= expect(malformed.open(QIODevice::WriteOnly | QIODevice::Truncate), "malformed fixture open failed");
    malformed.write("[machine\nid =");
    malformed.close();
    ok &= expect(!manager.loadTomlFile(path).has_value(), "malformed TOML must be rejected");

    return ok ? 0 : 1;
}
