#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QLineEdit>
#include <QTabWidget>
#include <QTableWidget>

#include <iostream>

#include "cutemac/config/Configuration.h"
#include "cutemac/machines/MachineCatalog.h"
#include "cutemac/ui/ConfigurationDialog.h"

namespace {

bool expect(bool condition, const char* message)
{
    if (!condition) std::cerr << "FAIL: " << message << '\n';
    return condition;
}

} // namespace

int main(int argc, char** argv)
{
#ifndef Q_OS_MACOS
    qputenv("QT_QPA_PLATFORM", "offscreen");
#endif
    QApplication app(argc, argv);
    auto configuration = cutemac::config::ConfigurationManager::defaultMacPlusConfiguration();
    configuration.nvramPath = QStringLiteral("/tmp/plus.nvram");
    configuration.scsiDevices.append({ 0, cutemac::config::ScsiDeviceType::HardDisk,
        QStringLiteral("/tmp/disk.hda"), false });
    configuration.skipRamPatternTest = true;
    cutemac::ui::ConfigurationDialog dialog(configuration);

    auto* machine = dialog.findChild<QComboBox*>(QStringLiteral("machineSelector"));
    auto* ram = dialog.findChild<QComboBox*>(QStringLiteral("ramSelector"));
    auto* patch = dialog.findChild<QCheckBox*>(QStringLiteral("skipRamPatternTest"));
    auto* nvram = dialog.findChild<QLineEdit*>(QStringLiteral("nvramPath"));
    auto* tabs = dialog.findChild<QTabWidget*>();
    auto* scsi = dialog.findChild<QWidget*>(QStringLiteral("scsiTab"));
    auto* scsiTable = dialog.findChild<QTableWidget*>(QStringLiteral("scsiDevices"));
    auto* nubus = dialog.findChild<QWidget*>(QStringLiteral("nubusTab"));
    auto* floppy = dialog.findChild<QWidget*>(QStringLiteral("floppyTab"));
    if (!expect(machine && ram && patch && nvram && tabs && scsi && scsiTable && nubus && floppy,
            "configuration controls must be available")) return 1;

    bool ok = true;
    ok &= expect(machine->findData(QStringLiteral("quadra-800")) < 0,
        "unimplemented Quadra 800 must not be selectable");
    ok &= expect(tabs->isTabVisible(tabs->indexOf(scsi)) && !tabs->isTabVisible(tabs->indexOf(nubus))
            && tabs->isTabVisible(tabs->indexOf(floppy)),
        "Mac Plus tabs must match its devices");
    auto* scsiType = qobject_cast<QComboBox*>(scsiTable->cellWidget(0, 1));
    auto* scsiAccess = qobject_cast<QComboBox*>(scsiTable->cellWidget(0, 3));
    if (!expect(scsiType && scsiAccess, "SCSI row controls must be available")) return 1;
    scsiType->setCurrentIndex(scsiType->findData(static_cast<int>(cutemac::config::ScsiDeviceType::CdRom)));
    ok &= expect(!scsiAccess->isEnabled() && scsiAccess->currentData().toBool()
            && dialog.configuration().scsiDevices.first().readOnly,
        "CD-ROM selections must force read-only access");

    machine->setCurrentIndex(machine->findData(QStringLiteral("mac-128k")));
    const auto compact = dialog.configuration();
    ok &= expect(!tabs->isTabVisible(tabs->indexOf(scsi)) && !tabs->isTabVisible(tabs->indexOf(nubus))
            && tabs->isTabVisible(tabs->indexOf(floppy)),
        "Macintosh 128K must hide unsupported SCSI and NuBus tabs");
    ok &= expect(!patch->isEnabled() && !nvram->isEnabled()
            && !compact.skipRamPatternTest && compact.nvramPath.isEmpty()
            && compact.scsiDevices.isEmpty() && compact.diskPath.isEmpty()
            && compact.ramSizeKiB == 128,
        "machine switch must discard unavailable device settings and select valid RAM");

    machine->setCurrentIndex(machine->findData(QStringLiteral("quadra-700")));
    ok &= expect(tabs->isTabVisible(tabs->indexOf(scsi)) && tabs->isTabVisible(tabs->indexOf(nubus))
            && tabs->isTabVisible(tabs->indexOf(floppy)) && !patch->isEnabled() && nvram->isEnabled(),
        "Q700 must expose SCSI, NuBus, floppy and NVRAM but not the RAM patch");
    ok &= expect(ram->count() == 2 && ram->itemData(0).toInt() == 4096
            && ram->itemData(1).toInt() == 8192,
        "Q700 RAM selector must contain only ROM-tested sizes");
    ram->setCurrentIndex(ram->findData(8192));
    machine->setCurrentIndex(machine->findData(QStringLiteral("mac-iicx")));
    ok &= expect(ram->currentData().toInt() == 8192 && patch->isEnabled(),
        "machine switch must retain a RAM size when valid for the new machine");

    auto iicxConfiguration = configuration;
    iicxConfiguration.machineId = QStringLiteral("mac-iicx");
    iicxConfiguration.nubusDevices.append({ 9, cutemac::config::NuBusDeviceType::CuteMacVideo });
    cutemac::ui::ConfigurationDialog cardDialog(iicxConfiguration);
    auto* cardMachine = cardDialog.findChild<QComboBox*>(QStringLiteral("machineSelector"));
    auto* cardTable = cardDialog.findChild<QTableWidget*>(QStringLiteral("nubusCards"));
    if (!expect(cardMachine && cardTable, "NuBus controls must be available")) return 1;
    cardMachine->setCurrentIndex(cardMachine->findData(QStringLiteral("quadra-700")));
    ok &= expect(cardTable->rowCount() == 0 && cardDialog.configuration().nubusDevices.isEmpty(),
        "switching from IIcx to Q700 must clear incompatible NuBus cards");
    cardMachine->setCurrentIndex(cardMachine->findData(QStringLiteral("mac-iicx")));
    ok &= expect(cardTable->rowCount() == 1 && cardDialog.configuration().nubusDevices.first().slot == 9,
        "switching back must restore the machine's unsaved NuBus selection");

    for (const auto& profile : cutemac::machines::MachineCatalog::supportedMachines()) {
        auto blank = cutemac::config::ConfigurationManager::defaultMacPlusConfiguration();
        blank.machineId = profile.id;
        blank.ramSizeKiB = profile.supportedRamSizesKiB.first();
        cutemac::ui::ConfigurationDialog machineDialog(blank);
        auto* machineTabs = machineDialog.findChild<QTabWidget*>();
        auto* machineScsi = machineDialog.findChild<QWidget*>(QStringLiteral("scsiTab"));
        auto* machineNuBus = machineDialog.findChild<QWidget*>(QStringLiteral("nubusTab"));
        auto* machineFloppy = machineDialog.findChild<QWidget*>(QStringLiteral("floppyTab"));
        auto* machineSerial = machineDialog.findChild<QWidget*>(QStringLiteral("serialTab"));
        if (!expect(machineTabs && machineScsi && machineNuBus && machineFloppy && machineSerial,
                "every machine dialog must expose the standard capability panels")) return 1;
        const auto hasPrefix = [&profile](const QString& prefix) {
            for (const auto& device : profile.reusableDevices) {
                if (device.startsWith(prefix)) return true;
            }
            return false;
        };
        ok &= expect(machineTabs->isTabVisible(machineTabs->indexOf(machineScsi)) == hasPrefix(QStringLiteral("device.scsi."))
                && machineTabs->isTabVisible(machineTabs->indexOf(machineNuBus)) == profile.reusableDevices.contains(QStringLiteral("device.nubus"))
                && machineTabs->isTabVisible(machineTabs->indexOf(machineFloppy))
                    == (profile.reusableDevices.contains(QStringLiteral("device.iwm"))
                        || profile.reusableDevices.contains(QStringLiteral("device.swim1")))
                && machineTabs->isTabVisible(machineTabs->indexOf(machineSerial)) == hasPrefix(QStringLiteral("device.scc.")),
            "machine tabs must follow the machine's declared devices");
        ok &= expect(cutemac::config::configurationValidationError(machineDialog.configuration()).isEmpty(),
            "default dialog configuration must be valid for every selectable machine");
    }
    return ok ? 0 : 1;
}
