#include <QFile>
#include <QTemporaryDir>

#include <iostream>

#include "cutemac/config/Configuration.h"
#include "cutemac/core/EmulationSession.h"
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
    configuration.machineId = QStringLiteral("mac-iicx");
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
    const auto ethernetBackend = cutemac::config::slirpNetworkingAvailable()
        ? cutemac::config::NetworkBackendType::Slirp
        : cutemac::config::NetworkBackendType::None;
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
        ok &= expect(loaded->nubusDevices.size() == 3 && loaded->nubusDevices.first().width == 832
                && loaded->nubusDevices.first().vramKiB == 4096
                && !loaded->nubusDevices.first().absolutePointer
                && loaded->nubusDevices[1].type == cutemac::config::NuBusDeviceType::CuteMacVideoAccelerated
                && loaded->nubusDevices[1].vramKiB == 8192
                && loaded->nubusDevices[2].type == cutemac::config::NuBusDeviceType::MacintoshIIVideo,
            "NuBus devices did not round-trip");
        ok &= expect(loaded->serialDevices.size() == 2 && loaded->serialDevices.first().channel == 1
                && loaded->serialDevices.first().outputDirectory == QStringLiteral("/tmp/prints")
                && loaded->serialDevices[1].type == cutemac::config::SerialDeviceType::HayesModem
                && loaded->serialDevices[1].directTcpDialing
                && loaded->serialDevices[1].phonebook.size() == 3
                && loaded->serialDevices[1].phonebook.first().number == QStringLiteral("1000")
                && loaded->serialDevices[1].phonebook.first().target == QStringLiteral("slip:libslirp")
                && loaded->serialDevices[1].phonebook[1].number == QStringLiteral("1001")
                && loaded->serialDevices[1].phonebook[1].target == QStringLiteral("ppp:libslirp")
                && loaded->serialDevices[1].phonebook[2].telnet,
            "serial devices did not round-trip");
        ok &= expect(loaded->enabledRomPatches() == QStringList { QStringLiteral("maciicx.skip_ram_pattern_test") },
            "enabled ROM patch ID is incorrect");
    }

    auto nullModemConfiguration = configuration;
    nullModemConfiguration.serialDevices[1] = nullModem;
    ok &= expect(manager.saveTomlFile(path, nullModemConfiguration), "null modem configuration save failed");
    const auto loadedNullModem = manager.loadTomlFile(path);
    ok &= expect(loadedNullModem && loadedNullModem->serialDevices.size() == 2
            && loadedNullModem->serialDevices[1].type == cutemac::config::SerialDeviceType::NullModem
            && loadedNullModem->serialDevices[1].tcpMode == cutemac::config::SerialTcpMode::Dial
            && loadedNullModem->serialDevices[1].tcpHost == QStringLiteral("debug.example.org")
            && loadedNullModem->serialDevices[1].tcpPort == 2323,
        "null modem settings did not round-trip");

    auto quadra = configuration;
    quadra.machineId = QStringLiteral("quadra-700");
    quadra.skipRamPatternTest = false;
    quadra.nubusDevices.clear();
    quadra.nubusDevices.append({ 13, cutemac::config::NuBusDeviceType::AppleDisplayCard824, {}, 640, 480, 8, 1024, false, true, cutemac::config::MacMonitorType::Rgb16Inch });
    quadra.nubusDevices.append({ 14, cutemac::config::NuBusDeviceType::AppleNuBusEthernet, {}, 640, 480, 8, 4096, true, true, cutemac::config::MacMonitorType::HiResRgb, ethernetBackend, QStringLiteral("02:00:1b:00:00:0e") });
    ok &= expect(manager.saveTomlFile(path, quadra), "Q700 slots 13 and 14 must save");
    const auto loadedQuadra = manager.loadTomlFile(path);
    ok &= expect(loadedQuadra && loadedQuadra->nubusDevices.size() == 2
            && loadedQuadra->nubusDevices[0].monitor == cutemac::config::MacMonitorType::Rgb16Inch
            && loadedQuadra->nubusDevices[1].networkBackend == ethernetBackend,
        "Q700 NuBus cards must round-trip");
    quadra.nubusDevices[0].slot = 9;
    ok &= expect(!manager.saveTomlFile(path, quadra), "Q700 must reject unavailable slot 9");
    QFile invalidQuadraSlot(path);
    ok &= expect(invalidQuadraSlot.open(QIODevice::WriteOnly | QIODevice::Truncate), "invalid Q700 slot fixture open failed");
    invalidQuadraSlot.write("[machine]\nid = 'quadra-700'\nram_size_kib = 4096\n[[nubus.devices]]\nslot = 9\ntype = 'apple_display_card_824'\nvram_kib = 1024\n");
    invalidQuadraSlot.close();
    ok &= expect(!manager.loadTomlFile(path).has_value(), "Q700 must reject unavailable slot 9 on load");
    quadra.nubusDevices[0].slot = 14;
    ok &= expect(!manager.saveTomlFile(path, quadra), "Q700 must reject duplicate slot 14");

    auto compactWithScsi = configuration;
    compactWithScsi.machineId = QStringLiteral("mac-128k");
    compactWithScsi.ramSizeKiB = 128;
    compactWithScsi.nvramPath.clear();
    compactWithScsi.skipRamPatternTest = false;
    compactWithScsi.nubusDevices.clear();
    ok &= expect(!manager.saveTomlFile(path, compactWithScsi),
        "compact Macintosh must reject a hidden SCSI disk");
    auto compactWithPatch = compactWithScsi;
    compactWithPatch.scsiDevices.clear();
    compactWithPatch.diskPath.clear();
    compactWithPatch.skipRamPatternTest = true;
    ok &= expect(!manager.saveTomlFile(path, compactWithPatch),
        "compact Macintosh must reject a RAM patch that has no implementation");
    auto duplicateScsi = configuration;
    duplicateScsi.scsiDevices.append(duplicateScsi.scsiDevices.first());
    ok &= expect(!manager.saveTomlFile(path, duplicateScsi),
        "duplicate SCSI target IDs must be rejected at save time");
    auto writableCd = configuration;
    writableCd.scsiDevices[0].type = cutemac::config::ScsiDeviceType::CdRom;
    ok &= expect(!manager.saveTomlFile(path, writableCd),
        "CD-ROM target must be read-only");
    auto duplicateSerial = configuration;
    duplicateSerial.serialDevices.append(duplicateSerial.serialDevices.last());
    ok &= expect(!manager.saveTomlFile(path, duplicateSerial),
        "duplicate serial channel attachments must be rejected");
    ok &= expect(!cutemac::machines::MachineCatalog::find(QStringLiteral("quadra-800")),
        "machine catalog must not offer a model with no machine factory implementation");
    for (const auto& profile : cutemac::machines::MachineCatalog::supportedMachines()) {
        auto runnable = cutemac::config::ConfigurationManager::defaultMacPlusConfiguration();
        runnable.machineId = profile.id;
        runnable.ramSizeKiB = profile.supportedRamSizesKiB.first();
        cutemac::core::EmulationSession session(runnable);
        ok &= expect(session.debugCpuAccess() != nullptr,
            "every selectable machine schema must have a matching machine factory");
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
                     { 9, cutemac::config::NuBusDeviceType::AppleDisplayCard824, {}, 640, 480, 8, 512, false, true, cutemac::config::MacMonitorType::Rgb16Inch })
            && cutemac::config::isValidNuBusDeviceConfiguration(
                { 9, cutemac::config::NuBusDeviceType::AppleDisplayCard824, {}, 640, 480, 8, 1024, false, true, cutemac::config::MacMonitorType::Rgb21Inch })
            && !cutemac::config::isValidNuBusDeviceConfiguration(
                { 9, cutemac::config::NuBusDeviceType::AppleDisplayCard824, {}, 640, 480, 8, 4096, false, true, cutemac::config::MacMonitorType::Rgb16Inch })
            && !cutemac::config::isValidNuBusDeviceConfiguration(
                { 9, cutemac::config::NuBusDeviceType::AppleDisplayCard824, {}, 640, 480, 8, 1024, false, true, cutemac::config::MacMonitorType::NtscMonitor }),
        "Apple Display Card 8-24 must accept only authentic 512 KiB and 1 MiB VRAM sizes");
    ok &= expect(cutemac::config::isValidNuBusDeviceConfiguration(
                     { 9, cutemac::config::NuBusDeviceType::AppleNuBusEthernet, {}, 640, 480, 8, 4096, true, true, cutemac::config::MacMonitorType::HiResRgb, cutemac::config::NetworkBackendType::None })
            && cutemac::config::isValidNuBusDeviceConfiguration(
                { 9, cutemac::config::NuBusDeviceType::AppleNuBusEthernet, {}, 640, 480, 8, 4096, true, true, cutemac::config::MacMonitorType::HiResRgb, cutemac::config::NetworkBackendType::Slirp })
                == cutemac::config::slirpNetworkingAvailable(),
        "Apple NuBus Ethernet backend choices must be gated by compiled networking support");

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
    auto invalidNullModemConfiguration = nullModemConfiguration;
    invalidNullModemConfiguration.serialDevices[1].tcpPort = 0;
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
    ok &= expect(cutemac::machines::MachineCatalog::isValidRamSize(QStringLiteral("quadra-700"), 4096)
            && cutemac::machines::MachineCatalog::isValidRamSize(QStringLiteral("quadra-700"), 8192)
            && !cutemac::machines::MachineCatalog::isValidRamSize(QStringLiteral("quadra-700"), 36864),
        "Q700 catalog must offer only RAM layouts that pass ROM sizing and reach DAFB video");
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

    const auto rejectUnknownChoice = [&](const QByteArray& contents, const char* description) {
        QFile profile(path);
        if (!profile.open(QIODevice::WriteOnly | QIODevice::Truncate)) return expect(false, "invalid choice fixture open failed");
        profile.write(contents);
        profile.close();
        return expect(!manager.loadTomlFile(path).has_value(), description);
    };
    ok &= rejectUnknownChoice("[machine]\nid = \"mac-plus\"\n[runtime]\nspeed = \"fast\"\n",
        "unknown runtime speed must be rejected");
    ok &= rejectUnknownChoice("[machine]\nid = \"mac-iicx\"\n[[scsi.devices]]\nid = 0\ntype = \"tape\"\nimage_path = \"tape.img\"\n",
        "unknown SCSI device type must be rejected");
    ok &= rejectUnknownChoice("[machine]\nid = \"mac-iicx\"\n[[nubus.devices]]\nslot = 9\ntype = \"mystery_card\"\n",
        "unknown NuBus card type must be rejected");
    ok &= rejectUnknownChoice("[machine]\nid = \"mac-iicx\"\n[[nubus.devices]]\nslot = 9\nmonitor = \"mystery_monitor\"\n",
        "unknown monitor type must be rejected");
    ok &= rejectUnknownChoice("[machine]\nid = \"mac-iicx\"\n[[nubus.devices]]\nslot = 9\nnetwork_backend = \"mystery_backend\"\n",
        "unknown network backend must be rejected");
    ok &= rejectUnknownChoice("[machine]\nid = \"mac-plus\"\n[[serial.devices]]\nchannel = 0\ntype = \"mystery_printer\"\n",
        "unknown serial device type must be rejected");
    ok &= rejectUnknownChoice("[machine]\nid = \"mac-plus\"\n[[serial.devices]]\nchannel = 0\ntcp_mode = \"relay\"\n",
        "unknown serial TCP mode must be rejected");

    return ok ? 0 : 1;
}
