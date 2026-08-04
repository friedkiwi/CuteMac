#include "cutemac/config/Configuration.h"

#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QSet>

#include <toml++/toml.hpp>

#include <sstream>

#include "cutemac/machines/MachineCatalog.h"

namespace cutemac::config {

namespace {

QString fromTomlString(const std::string& value)
{
    return QString::fromUtf8(value.data(), static_cast<qsizetype>(value.size()));
}

std::string toTomlString(const QString& value)
{
    const auto utf8 = value.toUtf8();
    return std::string(utf8.constData(), static_cast<std::size_t>(utf8.size()));
}

QString monitorName(MacMonitorType monitor)
{
    switch (monitor) {
    case MacMonitorType::Rgb21Inch: return QStringLiteral("rgb_21_inch");
    case MacMonitorType::PortraitMono15Inch: return QStringLiteral("portrait_mono_15_inch");
    case MacMonitorType::Rgb12Inch: return QStringLiteral("rgb_12_inch");
    case MacMonitorType::TwoPageMono21Inch: return QStringLiteral("two_page_mono_21_inch");
    case MacMonitorType::NtscMonitor: return QStringLiteral("ntsc_monitor");
    case MacMonitorType::PortraitRgb15Inch: return QStringLiteral("portrait_rgb_15_inch");
    case MacMonitorType::HiResRgb: return QStringLiteral("hi_res_rgb");
    case MacMonitorType::MultipleScan14Inch: return QStringLiteral("multiple_scan_14_inch");
    case MacMonitorType::MultipleScan16Inch: return QStringLiteral("multiple_scan_16_inch");
    case MacMonitorType::MultipleScan21Inch: return QStringLiteral("multiple_scan_21_inch");
    case MacMonitorType::PalEncoder: return QStringLiteral("pal_encoder");
    case MacMonitorType::NtscEncoder: return QStringLiteral("ntsc_encoder");
    case MacMonitorType::Vga: return QStringLiteral("vga");
    case MacMonitorType::Rgb16Inch: return QStringLiteral("rgb_16_inch");
    case MacMonitorType::PalMonitor: return QStringLiteral("pal_monitor");
    case MacMonitorType::Rgb19Inch: return QStringLiteral("rgb_19_inch");
    }
    return QStringLiteral("hi_res_rgb");
}

MacMonitorType monitorFromName(const QString& name)
{
    if (name == QStringLiteral("rgb_21_inch")) return MacMonitorType::Rgb21Inch;
    if (name == QStringLiteral("portrait_mono_15_inch")) return MacMonitorType::PortraitMono15Inch;
    if (name == QStringLiteral("rgb_12_inch")) return MacMonitorType::Rgb12Inch;
    if (name == QStringLiteral("two_page_mono_21_inch")) return MacMonitorType::TwoPageMono21Inch;
    if (name == QStringLiteral("ntsc_monitor")) return MacMonitorType::NtscMonitor;
    if (name == QStringLiteral("portrait_rgb_15_inch")) return MacMonitorType::PortraitRgb15Inch;
    if (name == QStringLiteral("hi_res_rgb")) return MacMonitorType::HiResRgb;
    if (name == QStringLiteral("multiple_scan_14_inch")) return MacMonitorType::MultipleScan14Inch;
    if (name == QStringLiteral("multiple_scan_16_inch")) return MacMonitorType::MultipleScan16Inch;
    if (name == QStringLiteral("multiple_scan_21_inch")) return MacMonitorType::MultipleScan21Inch;
    if (name == QStringLiteral("pal_encoder")) return MacMonitorType::PalEncoder;
    if (name == QStringLiteral("ntsc_encoder")) return MacMonitorType::NtscEncoder;
    if (name == QStringLiteral("vga")) return MacMonitorType::Vga;
    if (name == QStringLiteral("rgb_16_inch")) return MacMonitorType::Rgb16Inch;
    if (name == QStringLiteral("pal_monitor")) return MacMonitorType::PalMonitor;
    if (name == QStringLiteral("rgb_19_inch")) return MacMonitorType::Rgb19Inch;
    return MacMonitorType::HiResRgb;
}

QString networkBackendName(NetworkBackendType backend)
{
    switch (backend) {
    case NetworkBackendType::Slirp: return QStringLiteral("slirp");
    case NetworkBackendType::None:
    default:
        return QStringLiteral("none");
    }
}

NetworkBackendType networkBackendFromName(const QString& name)
{
    return name == QStringLiteral("slirp") ? NetworkBackendType::Slirp : NetworkBackendType::None;
}

bool isAppleDisplayCard824Monitor(MacMonitorType monitor)
{
    switch (monitor) {
    case MacMonitorType::Rgb21Inch:
    case MacMonitorType::PortraitMono15Inch:
    case MacMonitorType::Rgb12Inch:
    case MacMonitorType::TwoPageMono21Inch:
    case MacMonitorType::HiResRgb:
    case MacMonitorType::Rgb16Inch:
        return true;
    default:
        return false;
    }
}

QString safeProfileFileBase(QString profileName)
{
    profileName = profileName.trimmed();
    if (profileName.isEmpty()) {
        profileName = QStringLiteral("Mac Plus");
    }

    profileName.replace(QRegularExpression(QStringLiteral(R"([^A-Za-z0-9._-]+)")), QStringLiteral("_"));
    profileName.replace(QRegularExpression(QStringLiteral(R"(^_+|_+$)")), QString());
    return profileName.isEmpty() ? QStringLiteral("Mac_Plus") : profileName;
}

bool machineHasFloppyController(const QString& machineId)
{
    const auto machine = machines::MachineCatalog::find(machineId);
    if (!machine) return false;
    return machine->reusableDevices.contains(QStringLiteral("device.iwm"))
        || machine->reusableDevices.contains(QStringLiteral("device.swim1"));
}

void normalizeFloppyDevices(Configuration& configuration)
{
    if (!machineHasFloppyController(configuration.machineId)) {
        configuration.iwmDevices.clear();
        configuration.floppyPath.clear();
        return;
    }
    if (configuration.iwmDevices.isEmpty()) {
        configuration.iwmDevices.append({ configuration.floppyPath, false });
    }
    while (configuration.iwmDevices.size() < 2) {
        configuration.iwmDevices.append(IwmDeviceConfiguration {});
    }
    if (configuration.iwmDevices.size() > 2) {
        configuration.iwmDevices.resize(2);
    }
    configuration.floppyPath = configuration.iwmDevices.first().imagePath;
}

} // namespace

bool isCuteMacVideoDevice(NuBusDeviceType type)
{
    return type == NuBusDeviceType::CuteMacVideo || type == NuBusDeviceType::CuteMacVideoAccelerated;
}

bool slirpNetworkingAvailable()
{
    return CUTEMAC_HAS_LIBSLIRP != 0;
}

int cuteMacVideoFramebufferLimitBytes()
{
    return 0x000e0000;
}

int framebufferStrideBytes(int width, int depth)
{
    return ((width * depth + 31) / 32) * 4;
}

bool isValidNuBusDeviceConfiguration(const NuBusDeviceConfiguration& device)
{
    if (device.slot < 9 || device.slot > 14) return false;
    if (device.type == NuBusDeviceType::MacintoshIIVideo) return true;
    if (device.type == NuBusDeviceType::AppleDisplayCard824) {
        return (device.vramKiB == 512 || device.vramKiB == 1024)
            && isAppleDisplayCard824Monitor(device.monitor);
    }
    if (device.type == NuBusDeviceType::AppleNuBusEthernet) {
        return device.networkBackend == NetworkBackendType::None
            || (device.networkBackend == NetworkBackendType::Slirp && slirpNetworkingAvailable());
    }
    if (!isCuteMacVideoDevice(device.type)) return false;
    if (device.width < 320 || device.height < 200) return false;
    if (device.depth != 1 && device.depth != 2 && device.depth != 4
        && device.depth != 8 && device.depth != 16 && device.depth != 32) return false;
    if (device.vramKiB < 1024 || device.vramKiB > 14 * 1024) return false;
    if ((device.vramKiB % 1024) != 0) return false;
    const auto stride = framebufferStrideBytes(device.width, device.depth);
    return stride > 0 && stride * device.height <= cuteMacVideoFramebufferLimitBytes();
}

QVector<SerialPhonebookEntry> defaultSerialModemPhonebook()
{
    return {
        { QStringLiteral("1000"), QStringLiteral("slip:libslirp"), false },
        { QStringLiteral("1001"), QStringLiteral("ppp:libslirp"), false },
    };
}

bool isValidSerialDeviceConfiguration(const SerialDeviceConfiguration& device)
{
    if (device.channel < 0 || device.channel > 1) return false;
    if (device.type == SerialDeviceType::ImageWriterII) return !device.outputDirectory.trimmed().isEmpty();
    if (device.type == SerialDeviceType::NullModem) {
        if (device.tcpPort < 1 || device.tcpPort > 65535) return false;
        if (device.tcpMode == SerialTcpMode::Dial && device.tcpHost.trimmed().isEmpty()) return false;
        return true;
    }
    if (device.type != SerialDeviceType::HayesModem) return false;
    if (device.slip.enabled) {
        if (device.slip.localIp.trimmed().isEmpty() || device.slip.remoteIp.trimmed().isEmpty()) return false;
        if (device.slip.mtu < 296 || device.slip.mtu > 1006) return false;
    }
    QSet<QString> numbers;
    for (const auto& entry : device.phonebook) {
        const auto number = entry.number.trimmed();
        const auto target = entry.target.trimmed();
        if (number.isEmpty() || target.isEmpty()) return false;
        if (numbers.contains(number)) return false;
        numbers.insert(number);
        if ((target == QStringLiteral("slip:libslirp") || target == QStringLiteral("ppp:libslirp")) && !device.slip.enabled) return false;
        if (!target.startsWith(QStringLiteral("slip:")) && !target.startsWith(QStringLiteral("ppp:")) && !target.startsWith(QStringLiteral("tcp:"))) return false;
    }
    return true;
}

QString runtimeSpeedName(RuntimeSpeed speed)
{
    switch (speed) {
    case RuntimeSpeed::Unlimited:
        return QStringLiteral("unlimited");
    case RuntimeSpeed::Realtime:
    default:
        return QStringLiteral("realtime");
    }
}

RuntimeSpeed runtimeSpeedFromName(const QString& name)
{
    return name.compare(QStringLiteral("unlimited"), Qt::CaseInsensitive) == 0
        ? RuntimeSpeed::Unlimited
        : RuntimeSpeed::Realtime;
}

QStringList Configuration::enabledRomPatches() const
{
    QStringList patches;
    if (skipRamPatternTest && machineId == QStringLiteral("mac-iicx")) {
        patches.append(QStringLiteral("maciicx.skip_ram_pattern_test"));
    } else if (skipRamPatternTest && machineId == QStringLiteral("mac-plus")) {
        patches.append(QStringLiteral("macplus.skip_ram_pattern_test"));
    } else if (skipRamPatternTest && machineId == QStringLiteral("quadra-700")) {
        patches.append(QStringLiteral("quadra700.skip_ram_pattern_test"));
    }
    return patches;
}

QString ConfigurationManager::configRootPath()
{
    const auto path = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    return path.isEmpty() ? QDir::homePath() + QStringLiteral("/.config/CuteMac") : path;
}

QString ConfigurationManager::profileDirectoryPath()
{
    return QDir(configRootPath()).filePath(QStringLiteral("profiles"));
}

QString ConfigurationManager::romDirectoryPath()
{
    const auto path = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    return QDir(path.isEmpty() ? configRootPath() : path).filePath(QStringLiteral("roms"));
}

QString ConfigurationManager::diskImageDirectoryPath()
{
    const auto path = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    return QDir(path.isEmpty() ? configRootPath() : path).filePath(QStringLiteral("disk-images"));
}

Configuration ConfigurationManager::defaultMacPlusConfiguration()
{
    Configuration configuration;
    configuration.profileName = QStringLiteral("Mac Plus");
    configuration.machineId = QStringLiteral("mac-plus");
    configuration.ramSizeKiB = 4096;
    configuration.cyclesPerFrame = 130560;
    configuration.iwmDevices.append(IwmDeviceConfiguration {});
    configuration.iwmDevices.append(IwmDeviceConfiguration {});
    return configuration;
}

bool ConfigurationManager::ensureDirectories() const
{
    QDir dir;
    return dir.mkpath(profileDirectoryPath())
        && dir.mkpath(romDirectoryPath())
        && dir.mkpath(diskImageDirectoryPath());
}

QVector<QString> ConfigurationManager::profileFilePaths() const
{
    const QDir directory(profileDirectoryPath());
    const auto entries = directory.entryInfoList({ QStringLiteral("*.toml") }, QDir::Files, QDir::Name);

    QVector<QString> paths;
    paths.reserve(entries.size());
    for (const auto& entry : entries) {
        paths.append(entry.absoluteFilePath());
    }
    return paths;
}

QString ConfigurationManager::profilePathForName(const QString& profileName) const
{
    return QDir(profileDirectoryPath()).filePath(safeProfileFileBase(profileName) + QStringLiteral(".toml"));
}

std::optional<Configuration> ConfigurationManager::loadTomlFile(const QString& path) const
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return std::nullopt;
    }

    auto configuration = defaultMacPlusConfiguration();
    configuration.iwmDevices.clear();
    try {
        const auto contents = file.readAll();
        const auto document = toml::parse(std::string_view(contents.constData(), static_cast<std::size_t>(contents.size())));
        configuration.profileName = fromTomlString(document["name"].value_or<std::string>(""));
        configuration.machineId = fromTomlString(document["machine"]["id"].value_or<std::string>(""));
        if (configuration.machineId == QStringLiteral("powermac-6100")) {
            configuration.machineId = QStringLiteral("powermac-8100");
        }
        configuration.romPath = fromTomlString(document["storage"]["rom_path"].value_or<std::string>(""));
        configuration.nvramPath = fromTomlString(document["storage"]["nvram_path"].value_or<std::string>(""));
        configuration.diskPath = fromTomlString(document["storage"]["disk_path"].value_or<std::string>(""));
        configuration.floppyPath = fromTomlString(document["storage"]["floppy_path"].value_or<std::string>(""));
        if (const auto sizeKiB = document["machine"]["ram_size_kib"].value<std::int64_t>()) {
            configuration.ramSizeKiB = static_cast<int>(*sizeKiB);
        } else if (const auto sizeMiB = document["machine"]["ram_size_mib"].value<std::int64_t>()) {
            configuration.ramSizeKiB = static_cast<int>(*sizeMiB) * 1024;
        } else if (!machines::MachineCatalog::isValidRamSize(configuration.machineId, configuration.ramSizeKiB)) {
            const auto machine = machines::MachineCatalog::find(configuration.machineId);
            if (!machine || machine->supportedRamSizesKiB.isEmpty()) return std::nullopt;
            configuration.ramSizeKiB = machine->supportedRamSizesKiB.first();
        }
        if (!machines::MachineCatalog::isValidRamSize(configuration.machineId, configuration.ramSizeKiB)) {
            return std::nullopt;
        }
        configuration.cyclesPerFrame = static_cast<int>(document["machine"]["cycles_per_frame"].value_or<std::int64_t>(configuration.cyclesPerFrame));
        configuration.runtimeSpeed = runtimeSpeedFromName(fromTomlString(document["runtime"]["speed"].value_or<std::string>("unlimited")));
        configuration.skipRamPatternTest = document["rom_patches"]["skip_ram_pattern_test"].value_or(false);

        if (const auto* devices = document["iwm"]["drives"].as_array()) {
            for (const auto& node : *devices) {
                if (const auto* drive = node.as_table()) {
                    configuration.iwmDevices.append({
                        fromTomlString((*drive)["image_path"].value_or<std::string>("")),
                        (*drive)["read_only"].value_or(false),
                    });
                }
            }
        }
        if (const auto* devices = document["scsi"]["devices"].as_array()) {
            for (const auto& node : *devices) {
                if (const auto* device = node.as_table()) {
                    const auto type = fromTomlString((*device)["type"].value_or<std::string>("hard_disk"));
                    configuration.scsiDevices.append({
                        static_cast<int>((*device)["id"].value_or<std::int64_t>(0)),
                        type == QStringLiteral("cd_rom") ? ScsiDeviceType::CdRom : ScsiDeviceType::HardDisk,
                        fromTomlString((*device)["image_path"].value_or<std::string>("")),
                        (*device)["read_only"].value_or(false),
                    });
                }
            }
        }
        if (const auto* devices = document["nubus"]["devices"].as_array()) {
            for (const auto& node : *devices) {
                if (const auto* device = node.as_table()) {
                    const auto type = fromTomlString((*device)["type"].value_or<std::string>("cutemac_video"));
                    const auto nubusType = type == QStringLiteral("apple_m2_video") ? NuBusDeviceType::MacintoshIIVideo
                        : type == QStringLiteral("apple_display_card_824") ? NuBusDeviceType::AppleDisplayCard824
                        : type == QStringLiteral("apple_nubus_ethernet")   ? NuBusDeviceType::AppleNuBusEthernet
                        : type == QStringLiteral("cutemac_video_accelerated") ? NuBusDeviceType::CuteMacVideoAccelerated
                                                                             : NuBusDeviceType::CuteMacVideo;
                    const auto vramKiB = (*device)["vram_kib"].value_or<std::int64_t>(
                        (*device)["vram_mib"].value_or<std::int64_t>(4) * 1024);
                    configuration.nubusDevices.append({
                        static_cast<int>((*device)["slot"].value_or<std::int64_t>(9)),
                        nubusType,
                        fromTomlString((*device)["declaration_rom_path"].value_or<std::string>("")),
                        static_cast<int>((*device)["width"].value_or<std::int64_t>(640)),
                        static_cast<int>((*device)["height"].value_or<std::int64_t>(480)),
                        static_cast<int>((*device)["depth"].value_or<std::int64_t>(8)),
                        static_cast<int>(vramKiB),
                        (*device)["acceleration"].value_or(true),
                        (*device)["absolute_pointer"].value_or(true),
                        monitorFromName(fromTomlString((*device)["monitor"].value_or<std::string>("hi_res_rgb"))),
                        networkBackendFromName(fromTomlString((*device)["network_backend"].value_or<std::string>("none"))),
                        fromTomlString((*device)["mac_address"].value_or<std::string>("")),
                    });
                }
            }
        }
        if (const auto* devices = document["serial"]["devices"].as_array()) {
            for (const auto& node : *devices) {
                if (const auto* device = node.as_table()) {
                    const auto typeName = fromTomlString((*device)["type"].value_or<std::string>("imagewriter_ii"));
                    SerialDeviceConfiguration serial;
                    serial.channel = static_cast<int>((*device)["channel"].value_or<std::int64_t>(1));
                    serial.type = typeName == QStringLiteral("hayes_modem") ? SerialDeviceType::HayesModem
                        : typeName == QStringLiteral("null_modem")           ? SerialDeviceType::NullModem
                                                                             : SerialDeviceType::ImageWriterII;
                    serial.outputDirectory = fromTomlString((*device)["output_directory"].value_or<std::string>(""));
                    const auto tcpMode = fromTomlString((*device)["tcp_mode"].value_or<std::string>("listen"));
                    serial.tcpMode = tcpMode == QStringLiteral("dial") ? SerialTcpMode::Dial : SerialTcpMode::Listen;
                    serial.tcpHost = fromTomlString((*device)["tcp_host"].value_or<std::string>("127.0.0.1"));
                    serial.tcpPort = static_cast<int>((*device)["tcp_port"].value_or<std::int64_t>(0));
                    serial.directTcpDialing = (*device)["direct_tcp_dialing"].value_or(false);
                    if (const auto* slip = (*device)["slip"].as_table()) {
                        serial.slip.enabled = (*slip)["enabled"].value_or(true);
                        serial.slip.localIp = fromTomlString((*slip)["local_ip"].value_or<std::string>("172.16.0.1"));
                        serial.slip.remoteIp = fromTomlString((*slip)["remote_ip"].value_or<std::string>("172.16.0.2"));
                        serial.slip.mtu = static_cast<int>((*slip)["mtu"].value_or<std::int64_t>(1006));
                    }
                    if (const auto* phonebook = (*device)["phonebook"].as_array()) {
                        for (const auto& phoneNode : *phonebook) {
                            if (const auto* entry = phoneNode.as_table()) {
                                serial.phonebook.append({
                                    fromTomlString((*entry)["number"].value_or<std::string>("")),
                                    fromTomlString((*entry)["target"].value_or<std::string>("")),
                                    (*entry)["telnet"].value_or(false),
                                });
                            }
                        }
                    }
                    if (serial.type == SerialDeviceType::HayesModem && serial.phonebook.isEmpty()) {
                        serial.phonebook = defaultSerialModemPhonebook();
                    }
                    configuration.serialDevices.append(serial);
                }
            }
        }
    } catch (const toml::parse_error&) {
        return std::nullopt;
    }

    if (configuration.profileName.isEmpty()) {
        configuration.profileName = QFileInfo(path).baseName();
    }
    if (configuration.machineId.isEmpty()) {
        configuration.machineId = QStringLiteral("mac-plus");
    }
    normalizeFloppyDevices(configuration);
    if (configuration.scsiDevices.isEmpty() && !configuration.diskPath.isEmpty()) {
        configuration.scsiDevices.append({ 0, ScsiDeviceType::HardDisk, configuration.diskPath, false });
    } else if (!configuration.scsiDevices.isEmpty()) {
        configuration.diskPath = configuration.scsiDevices.first().imagePath;
    }
    for (const auto& device : configuration.nubusDevices) {
        if (!isValidNuBusDeviceConfiguration(device)) return std::nullopt;
    }
    for (const auto& device : configuration.serialDevices) {
        if (!isValidSerialDeviceConfiguration(device)) return std::nullopt;
    }

    return configuration;
}

bool ConfigurationManager::saveTomlFile(const QString& path, const Configuration& configuration) const
{
    if (!machines::MachineCatalog::isValidRamSize(configuration.machineId, configuration.ramSizeKiB)) {
        return false;
    }
    for (const auto& device : configuration.nubusDevices) {
        if (!isValidNuBusDeviceConfiguration(device)) return false;
    }
    for (const auto& device : configuration.serialDevices) {
        if (!isValidSerialDeviceConfiguration(device)) return false;
    }
    (void)ensureDirectories();

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
        return false;
    }

    toml::array iwmDevices;
    for (const auto& drive : configuration.iwmDevices) {
        iwmDevices.push_back(toml::table {
            { "image_path", toTomlString(drive.imagePath) },
            { "read_only", drive.readOnly },
        });
    }
    toml::array scsiDevices;
    for (const auto& device : configuration.scsiDevices) {
        scsiDevices.push_back(toml::table {
            { "id", device.id },
            { "type", device.type == ScsiDeviceType::CdRom ? "cd_rom" : "hard_disk" },
            { "image_path", toTomlString(device.imagePath) },
            { "read_only", device.readOnly },
        });
    }
    toml::array nubusDevices;
    for (const auto& device : configuration.nubusDevices) {
        toml::table nubusDevice {
            { "slot", device.slot },
            { "type", device.type == NuBusDeviceType::MacintoshIIVideo ? "apple_m2_video"
                    : device.type == NuBusDeviceType::AppleDisplayCard824 ? "apple_display_card_824"
                    : device.type == NuBusDeviceType::AppleNuBusEthernet ? "apple_nubus_ethernet"
                    : device.type == NuBusDeviceType::CuteMacVideoAccelerated ? "cutemac_video_accelerated"
                                                                             : "cutemac_video" },
            { "width", device.width },
            { "height", device.height },
            { "depth", device.depth },
            { "vram_kib", device.vramKiB },
            { "acceleration", device.acceleration },
            { "absolute_pointer", device.absolutePointer },
        };
        if (device.type == NuBusDeviceType::AppleDisplayCard824) {
            nubusDevice.insert("monitor", toTomlString(monitorName(device.monitor)));
        } else if (device.type == NuBusDeviceType::AppleNuBusEthernet) {
            nubusDevice.insert("network_backend", toTomlString(networkBackendName(device.networkBackend)));
            if (!device.macAddress.trimmed().isEmpty()) {
                nubusDevice.insert("mac_address", toTomlString(device.macAddress.trimmed()));
            }
        }
        nubusDevices.push_back(std::move(nubusDevice));
    }
    toml::array serialDevices;
    for (const auto& device : configuration.serialDevices) {
        toml::table serialDevice {
            { "channel", device.channel },
            { "type", device.type == SerialDeviceType::HayesModem ? "hayes_modem"
                    : device.type == SerialDeviceType::NullModem   ? "null_modem"
                                                                   : "imagewriter_ii" },
        };
        if (device.type == SerialDeviceType::ImageWriterII) {
            serialDevice.insert("output_directory", toTomlString(device.outputDirectory));
        } else if (device.type == SerialDeviceType::NullModem) {
            serialDevice.insert("tcp_mode", device.tcpMode == SerialTcpMode::Dial ? "dial" : "listen");
            serialDevice.insert("tcp_host", toTomlString(device.tcpHost));
            serialDevice.insert("tcp_port", device.tcpPort);
        } else {
            serialDevice.insert("direct_tcp_dialing", device.directTcpDialing);
            serialDevice.insert("slip", toml::table {
                { "enabled", device.slip.enabled },
                { "local_ip", toTomlString(device.slip.localIp) },
                { "remote_ip", toTomlString(device.slip.remoteIp) },
                { "mtu", device.slip.mtu },
            });
            toml::array phonebook;
            for (const auto& entry : device.phonebook) {
                phonebook.push_back(toml::table {
                    { "number", toTomlString(entry.number) },
                    { "target", toTomlString(entry.target) },
                    { "telnet", entry.telnet },
                });
            }
            serialDevice.insert("phonebook", std::move(phonebook));
        }
        serialDevices.push_back(std::move(serialDevice));
    }

    toml::table document {
        { "name", toTomlString(configuration.profileName) },
        { "version", 3 },
        { "machine", toml::table {
                         { "id", toTomlString(configuration.machineId) },
                         { "ram_size_kib", configuration.ramSizeKiB },
                         { "cycles_per_frame", configuration.cyclesPerFrame },
                     } },
        { "storage", toml::table {
                         { "nvram_path", toTomlString(configuration.nvramPath) },
                         { "disk_path", toTomlString(configuration.diskPath) },
                         { "floppy_path", toTomlString(configuration.floppyPath) },
                     } },
        { "runtime", toml::table {
                         { "speed", toTomlString(runtimeSpeedName(configuration.runtimeSpeed)) },
                     } },
        { "iwm", toml::table { { "drives", std::move(iwmDevices) } } },
        { "scsi", toml::table { { "devices", std::move(scsiDevices) } } },
        { "nubus", toml::table { { "devices", std::move(nubusDevices) } } },
        { "serial", toml::table { { "devices", std::move(serialDevices) } } },
        { "rom_patches", toml::table {
                             { "skip_ram_pattern_test", configuration.skipRamPatternTest },
                         } },
    };
    std::ostringstream stream;
    stream << document;
    const auto serialized = stream.str();
    return file.write(serialized.data(), static_cast<qint64>(serialized.size())) == static_cast<qint64>(serialized.size());
}

} // namespace cutemac::config
