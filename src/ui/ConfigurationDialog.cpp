#include "cutemac/ui/ConfigurationDialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QHeaderView>
#include <QInputMethodEvent>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QSaveFile>
#include <QSpinBox>
#include <QTabWidget>
#include <QTableWidget>
#include <QVBoxLayout>
#include <QSet>

#include <algorithm>
#include <array>

#include "cutemac/machines/MachineCatalog.h"
#include "cutemac/rom/RomCatalog.h"
#include "cutemac/ui/DiskImageWidgets.h"

namespace cutemac::ui {

class ConfigurationDialog::Impl {
public:
    config::Configuration original;
    QLineEdit* name = nullptr;
    QComboBox* machine = nullptr;
    QLineEdit* nvram = nullptr;
    bool nvramZapped = false;
    QComboBox* ram = nullptr;
    QComboBox* speed = nullptr;
    QCheckBox* skipRamTest = nullptr;
    QTabWidget* tabs = nullptr;
    QWidget* serialTab = nullptr;
    std::array<QComboBox*, 2> serialDevice {};
    std::array<QPushButton*, 2> serialConfigure {};
    std::array<config::SerialDeviceConfiguration, 2> serialConfiguration;
    QWidget* iwmTab = nullptr;
    std::array<QLineEdit*, 2> floppy {};
    std::array<QCheckBox*, 2> floppyReadOnly {};
    QWidget* scsiTab = nullptr;
    QTableWidget* scsi = nullptr;
    QWidget* nubusTab = nullptr;
    QTableWidget* nubus = nullptr;
    QList<config::NuBusDeviceConfiguration> nubusDevices;
};

namespace {

class NonEditableTableWidget final : public QTableWidget {
public:
    using QTableWidget::QTableWidget;

protected:
    bool edit(const QModelIndex&, EditTrigger, QEvent*) override { return false; }
    void inputMethodEvent(QInputMethodEvent* event) override { event->ignore(); }
};

QString nubusCardName(config::NuBusDeviceType type)
{
    switch (type) {
    case config::NuBusDeviceType::CuteMacVideo:
        return QStringLiteral("CuteMac Video");
    case config::NuBusDeviceType::CuteMacVideoAccelerated:
        return QStringLiteral("CuteMac Video Accelerated");
    case config::NuBusDeviceType::MacintoshIIVideo:
        return QStringLiteral("Apple Macintosh II Video Card");
    }
    return QStringLiteral("Unknown card");
}

QString nubusValidationMessage(const config::NuBusDeviceConfiguration& device)
{
    if (config::isValidNuBusDeviceConfiguration(device)) return {};
    if (config::isCuteMacVideoDevice(device.type)) {
        const auto bytes = config::framebufferStrideBytes(device.width, device.depth) * device.height;
        return QStringLiteral("CuteMac Video %1x%2 at %3-bit needs %4 bytes, but the 24-bit NuBus framebuffer window is limited to %5 bytes before MMIO.")
            .arg(device.width)
            .arg(device.height)
            .arg(device.depth)
            .arg(bytes)
            .arg(config::cuteMacVideoFramebufferLimitBytes());
    }
    return QStringLiteral("Invalid NuBus device configuration.");
}

QString serialDeviceName(config::SerialDeviceType type)
{
    switch (type) {
    case config::SerialDeviceType::NullModem:
        return QStringLiteral("Terminal/null modem");
    case config::SerialDeviceType::HayesModem:
        return QStringLiteral("Hayes modem");
    case config::SerialDeviceType::ImageWriterII:
    default:
        return QStringLiteral("ImageWriter II");
    }
}

bool editNuBusCard(config::NuBusDeviceConfiguration& device, QWidget* parent)
{
    QDialog dialog(parent);
    dialog.setWindowTitle(QStringLiteral("NuBus Card Properties"));
    auto* outer = new QVBoxLayout(&dialog);
    auto* form = new QFormLayout;
    outer->addLayout(form);

    auto* slot = new QComboBox;
    for (int value = 9; value <= 11; ++value) slot->addItem(QString::number(value), value);
    slot->setCurrentIndex(qMax(0, slot->findData(device.slot)));
    form->addRow(QStringLiteral("Slot"), slot);

    QSpinBox* width = nullptr;
    QSpinBox* height = nullptr;
    QComboBox* depth = nullptr;
    QSpinBox* vram = nullptr;
    QCheckBox* acceleration = nullptr;
    QCheckBox* absolutePointer = nullptr;

    if (device.type == config::NuBusDeviceType::CuteMacVideo
        || device.type == config::NuBusDeviceType::CuteMacVideoAccelerated) {
        width = new QSpinBox;
        width->setRange(320, 4096);
        width->setValue(device.width);
        form->addRow(QStringLiteral("Width"), width);
        height = new QSpinBox;
        height->setRange(200, 2160);
        height->setValue(device.height);
        form->addRow(QStringLiteral("Height"), height);
        depth = new QComboBox;
        for (const int value : {1, 2, 4, 8, 16, 32}) depth->addItem(QString::number(value), value);
        depth->setCurrentIndex(qMax(0, depth->findData(device.depth)));
        form->addRow(QStringLiteral("Color depth"), depth);
        vram = new QSpinBox;
        vram->setRange(1024, 14 * 1024);
        vram->setSingleStep(1024);
        vram->setSuffix(QStringLiteral(" KiB"));
        vram->setValue(device.vramKiB);
        form->addRow(QStringLiteral("VRAM"), vram);
        acceleration = new QCheckBox;
        acceleration->setChecked(device.acceleration);
        form->addRow(QStringLiteral("Acceleration"), acceleration);
        absolutePointer = new QCheckBox;
        absolutePointer->setChecked(device.absolutePointer);
        form->addRow(QStringLiteral("Absolute pointer integration"), absolutePointer);
    }

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    outer->addWidget(buttons);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, [&]() {
        auto candidate = device;
        candidate.slot = slot->currentData().toInt();
        if (candidate.type == config::NuBusDeviceType::CuteMacVideo
            || candidate.type == config::NuBusDeviceType::CuteMacVideoAccelerated) {
            candidate.width = width->value();
            candidate.height = height->value();
            candidate.depth = depth->currentData().toInt();
            candidate.vramKiB = vram->value();
        }
        const auto validation = nubusValidationMessage(candidate);
        if (!validation.isEmpty()) {
            QMessageBox::warning(&dialog, dialog.windowTitle(), validation);
            return;
        }
        dialog.accept();
    });
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() != QDialog::Accepted) return false;

    device.slot = slot->currentData().toInt();
    if (device.type == config::NuBusDeviceType::CuteMacVideo
        || device.type == config::NuBusDeviceType::CuteMacVideoAccelerated) {
        device.width = width->value();
        device.height = height->value();
        device.depth = depth->currentData().toInt();
        device.vramKiB = vram->value();
        device.acceleration = acceleration->isChecked();
        device.absolutePointer = absolutePointer->isChecked();
        device.declarationRomPath.clear();
    }
    return true;
}

bool editImageWriterII(QString& outputDirectory, QWidget* parent)
{
    QDialog dialog(parent);
    dialog.setWindowTitle(QStringLiteral("ImageWriter II Configuration"));
    auto* outer = new QVBoxLayout(&dialog);
    auto* form = new QFormLayout;
    outer->addLayout(form);
    auto* output = new QLineEdit(outputDirectory);
    auto* browse = new QPushButton(QStringLiteral("Browse..."));
    auto* row = new QHBoxLayout;
    row->addWidget(output, 1);
    row->addWidget(browse);
    form->addRow(QStringLiteral("PNG output folder"), row);
    QObject::connect(browse, &QPushButton::clicked, &dialog, [&dialog, output]() {
        const auto path = QFileDialog::getExistingDirectory(&dialog,
            QStringLiteral("Select Print Output Folder"), output->text());
        if (!path.isEmpty()) output->setText(path);
    });
    auto* note = new QLabel(QStringLiteral("Pages are written at 144 DPI using sequential filenames."));
    note->setWordWrap(true);
    outer->addWidget(note);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    outer->addWidget(buttons);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, [&dialog, output]() {
        if (output->text().trimmed().isEmpty()) {
            QMessageBox::warning(&dialog, dialog.windowTitle(), QStringLiteral("Select an output folder."));
            return;
        }
        dialog.accept();
    });
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() != QDialog::Accepted) return false;
    outputDirectory = output->text().trimmed();
    return true;
}

bool editHayesModem(config::SerialDeviceConfiguration& device, QWidget* parent)
{
    if (device.phonebook.isEmpty()) device.phonebook = config::defaultSerialModemPhonebook();

    QDialog dialog(parent);
    dialog.setWindowTitle(QStringLiteral("Hayes Modem Configuration"));
    dialog.resize(720, 520);
    auto* outer = new QVBoxLayout(&dialog);
    auto* form = new QFormLayout;
    outer->addLayout(form);

    auto* directTcp = new QCheckBox;
    directTcp->setChecked(device.directTcpDialing);
    form->addRow(QStringLiteral("Allow direct TCP dialing"), directTcp);

    auto* slipEnabled = new QCheckBox;
    slipEnabled->setChecked(device.slip.enabled);
    form->addRow(QStringLiteral("Enable SLIP backend"), slipEnabled);

    auto* localIp = new QLineEdit(device.slip.localIp);
    auto* remoteIp = new QLineEdit(device.slip.remoteIp);
    auto* mtu = new QSpinBox;
    mtu->setRange(296, 1006);
    mtu->setValue(device.slip.mtu);
    form->addRow(QStringLiteral("SLIP host IP"), localIp);
    form->addRow(QStringLiteral("SLIP guest IP"), remoteIp);
    form->addRow(QStringLiteral("SLIP MTU"), mtu);

    auto* note = new QLabel(QStringLiteral(
        "Default phonebook entry: dial 1000 to connect to the SLIP/libslirp backend.\n\n"
        "PPP is also available by default as phone number 1001 and can automatically negotiate the guest IP settings.\n\n"
        "Guest setup: install/configure MacTCP or Open Transport for SLIP on this serial port. "
        "Use the SLIP guest IP shown above as the Macintosh IP address, the SLIP host IP as router/gateway and DNS server, "
        "subnet mask 255.255.255.0, and MTU 1006 unless changed here. For PPP, choose PPP on this serial port and leave authentication blank or use any username/password."));
    note->setWordWrap(true);
    outer->addWidget(note);

    auto* phonebook = new QTableWidget(0, 3);
    phonebook->setHorizontalHeaderLabels({ QStringLiteral("Phone number"), QStringLiteral("Target"), QStringLiteral("Telnet") });
    phonebook->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    phonebook->setSelectionBehavior(QAbstractItemView::SelectRows);
    phonebook->setSelectionMode(QAbstractItemView::SingleSelection);
    outer->addWidget(new QLabel(QStringLiteral("Phonebook targets use slip:libslirp, ppp:libslirp, or tcp:host:port.")));
    outer->addWidget(phonebook, 1);

    auto addEntry = [phonebook](const config::SerialPhonebookEntry& entry) {
        const int row = phonebook->rowCount();
        phonebook->insertRow(row);
        phonebook->setItem(row, 0, new QTableWidgetItem(entry.number));
        phonebook->setItem(row, 1, new QTableWidgetItem(entry.target));
        auto* telnet = new QCheckBox;
        telnet->setChecked(entry.telnet);
        phonebook->setCellWidget(row, 2, telnet);
    };
    for (const auto& entry : device.phonebook) addEntry(entry);

    auto* phoneButtons = new QHBoxLayout;
    auto* add = new QPushButton(QStringLiteral("Add"));
    auto* addSlipDefault = new QPushButton(QStringLiteral("Add 1000 → SLIP"));
    auto* addPppDefault = new QPushButton(QStringLiteral("Add 1001 → PPP"));
    auto* remove = new QPushButton(QStringLiteral("Remove"));
    phoneButtons->addWidget(add);
    phoneButtons->addWidget(addSlipDefault);
    phoneButtons->addWidget(addPppDefault);
    phoneButtons->addWidget(remove);
    phoneButtons->addStretch();
    outer->addLayout(phoneButtons);

    QObject::connect(add, &QPushButton::clicked, &dialog, [&]() {
        addEntry({ QStringLiteral("5551212"), QStringLiteral("tcp:example.org:23"), true });
    });
    QObject::connect(addSlipDefault, &QPushButton::clicked, &dialog, [&]() {
        addEntry({ QStringLiteral("1000"), QStringLiteral("slip:libslirp"), false });
    });
    QObject::connect(addPppDefault, &QPushButton::clicked, &dialog, [&]() {
        addEntry({ QStringLiteral("1001"), QStringLiteral("ppp:libslirp"), false });
    });
    QObject::connect(remove, &QPushButton::clicked, &dialog, [&]() {
        if (phonebook->currentRow() >= 0) phonebook->removeRow(phonebook->currentRow());
    });

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    outer->addWidget(buttons);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, [&]() {
        config::SerialDeviceConfiguration candidate = device;
        candidate.type = config::SerialDeviceType::HayesModem;
        candidate.outputDirectory.clear();
        candidate.directTcpDialing = directTcp->isChecked();
        candidate.slip.enabled = slipEnabled->isChecked();
        candidate.slip.localIp = localIp->text().trimmed();
        candidate.slip.remoteIp = remoteIp->text().trimmed();
        candidate.slip.mtu = mtu->value();
        candidate.phonebook.clear();
        for (int row = 0; row < phonebook->rowCount(); ++row) {
            candidate.phonebook.append({
                phonebook->item(row, 0) ? phonebook->item(row, 0)->text().trimmed() : QString(),
                phonebook->item(row, 1) ? phonebook->item(row, 1)->text().trimmed() : QString(),
                qobject_cast<QCheckBox*>(phonebook->cellWidget(row, 2))->isChecked(),
            });
        }
        if (!config::isValidSerialDeviceConfiguration(candidate)) {
            QMessageBox::warning(&dialog, dialog.windowTitle(),
                QStringLiteral("Check the modem configuration. Phone numbers must be unique, targets must use slip:, ppp:, or tcp:, and SLIP/PPP targets require the network backend to be enabled."));
            return;
        }
        dialog.accept();
    });
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() != QDialog::Accepted) return false;

    device.type = config::SerialDeviceType::HayesModem;
    device.outputDirectory.clear();
    device.directTcpDialing = directTcp->isChecked();
    device.slip.enabled = slipEnabled->isChecked();
    device.slip.localIp = localIp->text().trimmed();
    device.slip.remoteIp = remoteIp->text().trimmed();
    device.slip.mtu = mtu->value();
    device.phonebook.clear();
    for (int row = 0; row < phonebook->rowCount(); ++row) {
        device.phonebook.append({
            phonebook->item(row, 0) ? phonebook->item(row, 0)->text().trimmed() : QString(),
            phonebook->item(row, 1) ? phonebook->item(row, 1)->text().trimmed() : QString(),
            qobject_cast<QCheckBox*>(phonebook->cellWidget(row, 2))->isChecked(),
        });
    }
    return true;
}

bool editNullModem(config::SerialDeviceConfiguration& device, QWidget* parent)
{
    QDialog dialog(parent);
    dialog.setWindowTitle(QStringLiteral("Terminal / Null Modem Configuration"));
    auto* outer = new QVBoxLayout(&dialog);
    auto* form = new QFormLayout;
    outer->addLayout(form);

    auto* mode = new QComboBox;
    mode->addItem(QStringLiteral("Listen for TCP connection"), static_cast<int>(config::SerialTcpMode::Listen));
    mode->addItem(QStringLiteral("Dial out to TCP host"), static_cast<int>(config::SerialTcpMode::Dial));
    mode->setCurrentIndex(qMax(0, mode->findData(static_cast<int>(device.tcpMode))));
    form->addRow(QStringLiteral("Mode"), mode);

    auto* host = new QLineEdit(device.tcpHost);
    auto* port = new QSpinBox;
    port->setRange(1, 65535);
    port->setValue(device.tcpPort > 0 ? device.tcpPort : 2323);
    form->addRow(QStringLiteral("Host / bind address"), host);
    form->addRow(QStringLiteral("TCP port"), port);

    auto* note = new QLabel(QStringLiteral(
        "This is a raw byte-stream attachment. Use listener mode to expose the Mac serial port on a local TCP port, "
        "or dial-out mode to connect the Mac serial port to an existing TCP service. It does not parse AT commands."));
    note->setWordWrap(true);
    outer->addWidget(note);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    outer->addWidget(buttons);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, [&]() {
        config::SerialDeviceConfiguration candidate = device;
        candidate.type = config::SerialDeviceType::NullModem;
        candidate.tcpMode = static_cast<config::SerialTcpMode>(mode->currentData().toInt());
        candidate.tcpHost = host->text().trimmed();
        candidate.tcpPort = port->value();
        if (!config::isValidSerialDeviceConfiguration(candidate)) {
            QMessageBox::warning(&dialog, dialog.windowTitle(), QStringLiteral("Enter a valid TCP host/address and port."));
            return;
        }
        dialog.accept();
    });
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() != QDialog::Accepted) return false;

    device.type = config::SerialDeviceType::NullModem;
    device.outputDirectory.clear();
    device.tcpMode = static_cast<config::SerialTcpMode>(mode->currentData().toInt());
    device.tcpHost = host->text().trimmed();
    device.tcpPort = port->value();
    return true;
}

} // namespace

ConfigurationDialog::ConfigurationDialog(config::Configuration configuration, QWidget* parent)
    : QDialog(parent)
    , m_impl(std::make_unique<Impl>())
{
    m_impl->original = std::move(configuration);
    setWindowTitle(QStringLiteral("Machine Configuration"));
    resize(680, 460);
    auto* outer = new QVBoxLayout(this);
    m_impl->tabs = new QTabWidget;
    outer->addWidget(m_impl->tabs, 1);

    auto* general = new QWidget;
    auto* form = new QFormLayout(general);
    m_impl->name = new QLineEdit(m_impl->original.profileName);
    form->addRow(QStringLiteral("Name"), m_impl->name);
    m_impl->machine = new QComboBox;
    for (const auto& machine : machines::MachineCatalog::supportedMachines()) {
        m_impl->machine->addItem(machine.displayName, machine.id);
    }
    m_impl->machine->setCurrentIndex(qMax(0, m_impl->machine->findData(m_impl->original.machineId)));
    form->addRow(QStringLiteral("Machine"), m_impl->machine);

    m_impl->nvram = new QLineEdit(m_impl->original.nvramPath);
    auto* nvramBrowse = new QPushButton(QStringLiteral("Browse..."));
    auto* nvramNew = new QPushButton(QStringLiteral("New..."));
    auto* nvramZap = new QPushButton(QStringLiteral("Zap..."));
    auto* nvramRow = new QHBoxLayout;
    nvramRow->addWidget(m_impl->nvram, 1);
    nvramRow->addWidget(nvramBrowse);
    nvramRow->addWidget(nvramNew);
    nvramRow->addWidget(nvramZap);
    form->addRow(QStringLiteral("NVRAM image"), nvramRow);
    m_impl->ram = new QComboBox;
    form->addRow(QStringLiteral("RAM"), m_impl->ram);
    m_impl->speed = new QComboBox;
    m_impl->speed->addItem(QStringLiteral("Unlimited"), static_cast<int>(config::RuntimeSpeed::Unlimited));
    m_impl->speed->addItem(QStringLiteral("Realtime"), static_cast<int>(config::RuntimeSpeed::Realtime));
    m_impl->speed->setCurrentIndex(m_impl->speed->findData(static_cast<int>(m_impl->original.runtimeSpeed)));
    form->addRow(QStringLiteral("Emulation speed"), m_impl->speed);
    m_impl->skipRamTest = new QCheckBox;
    m_impl->skipRamTest->setChecked(m_impl->original.skipRamPatternTest);
    form->addRow(QStringLiteral("Skip RAM pattern test"), m_impl->skipRamTest);
    m_impl->tabs->addTab(general, QStringLiteral("General"));

    m_impl->serialTab = new QWidget;
    auto* serialForm = new QFormLayout(m_impl->serialTab);
    const std::array<QString, 2> portNames { QStringLiteral("Modem port"), QStringLiteral("Printer port") };
    for (int channel = 0; channel < 2; ++channel) {
        auto* device = new QComboBox;
        device->addItem(QStringLiteral("None"), -1);
        device->addItem(QStringLiteral("ImageWriter II"), static_cast<int>(config::SerialDeviceType::ImageWriterII));
        device->addItem(QStringLiteral("Hayes modem"), static_cast<int>(config::SerialDeviceType::HayesModem));
        device->addItem(QStringLiteral("Terminal / null modem"), static_cast<int>(config::SerialDeviceType::NullModem));
        auto* configure = new QPushButton(QStringLiteral("Configure..."));
        auto* row = new QHBoxLayout;
        row->addWidget(device, 1);
        row->addWidget(configure);
        serialForm->addRow(portNames[channel], row);
        m_impl->serialDevice[channel] = device;
        m_impl->serialConfigure[channel] = configure;
        m_impl->serialConfiguration[channel].channel = channel;
        m_impl->serialConfiguration[channel].type = config::SerialDeviceType::ImageWriterII;
        const auto configured = std::find_if(m_impl->original.serialDevices.cbegin(), m_impl->original.serialDevices.cend(),
            [channel](const auto& entry) { return entry.channel == channel; });
        if (configured != m_impl->original.serialDevices.cend()) {
            device->setCurrentIndex(device->findData(static_cast<int>(configured->type)));
            m_impl->serialConfiguration[channel] = *configured;
        }
        configure->setEnabled(device->currentData().toInt() >= 0);
        connect(device, &QComboBox::currentIndexChanged, configure,
            [this, channel, device, configure]() {
                configure->setEnabled(device->currentData().toInt() >= 0);
                if (device->currentData().toInt() >= 0) {
                    auto& serial = m_impl->serialConfiguration[channel];
                    serial.channel = channel;
                    serial.type = static_cast<config::SerialDeviceType>(device->currentData().toInt());
                    if (serial.type == config::SerialDeviceType::HayesModem && serial.phonebook.isEmpty()) {
                        serial.phonebook = config::defaultSerialModemPhonebook();
                    }
                }
            });
        connect(configure, &QPushButton::clicked, this, [this, channel]() {
            auto& serial = m_impl->serialConfiguration[channel];
            serial.channel = channel;
            serial.type = static_cast<config::SerialDeviceType>(m_impl->serialDevice[channel]->currentData().toInt());
            if (serial.type == config::SerialDeviceType::ImageWriterII) {
                (void)editImageWriterII(serial.outputDirectory, this);
            } else if (serial.type == config::SerialDeviceType::HayesModem) {
                (void)editHayesModem(serial, this);
            } else if (serial.type == config::SerialDeviceType::NullModem) {
                (void)editNullModem(serial, this);
            }
        });
    }
    m_impl->tabs->addTab(m_impl->serialTab, QStringLiteral("Serial"));

    m_impl->iwmTab = new QWidget;
    auto* iwmForm = new QFormLayout(m_impl->iwmTab);
    auto configuredFloppy = [this](int drive) {
        if (drive >= 0 && drive < m_impl->original.iwmDevices.size()) return m_impl->original.iwmDevices[drive];
        return config::IwmDeviceConfiguration {};
    };
    const std::array<QString, 2> floppyLabels {
        QStringLiteral("Internal floppy"),
        QStringLiteral("External floppy"),
    };
    for (int drive = 0; drive < 2; ++drive) {
        const auto configured = configuredFloppy(drive);
        m_impl->floppy[drive] = new QLineEdit(drive == 0 && configured.imagePath.isEmpty()
                ? m_impl->original.floppyPath
                : configured.imagePath);
        auto* floppyBrowse = new QPushButton(QStringLiteral("Browse..."));
        auto* floppyNew = new QPushButton(QStringLiteral("New..."));
        auto* floppyRow = new QHBoxLayout;
        floppyRow->addWidget(m_impl->floppy[drive], 1);
        floppyRow->addWidget(floppyBrowse);
        floppyRow->addWidget(floppyNew);
        iwmForm->addRow(floppyLabels[drive], floppyRow);
        m_impl->floppyReadOnly[drive] = new QCheckBox;
        m_impl->floppyReadOnly[drive]->setChecked(configured.readOnly);
        iwmForm->addRow(QStringLiteral("%1 read-only").arg(floppyLabels[drive]), m_impl->floppyReadOnly[drive]);
        connect(floppyBrowse, &QPushButton::clicked, this, [this, drive]() {
            const auto path = DiskImagePickerDialog::getImage(storage::DiskImageType::Floppy, QStringLiteral("Select Floppy Image"), this);
            if (!path.isEmpty()) m_impl->floppy[drive]->setText(path);
        });
        connect(floppyNew, &QPushButton::clicked, this, [this, drive]() {
            const auto path = createDiskImage(storage::DiskImageType::Floppy, this);
            if (!path.isEmpty()) m_impl->floppy[drive]->setText(path);
        });
    }
    m_impl->tabs->addTab(m_impl->iwmTab, QStringLiteral("IWM"));

    m_impl->scsiTab = new QWidget;
    auto* scsiLayout = new QVBoxLayout(m_impl->scsiTab);
    m_impl->scsi = new NonEditableTableWidget(0, 4);
    m_impl->scsi->setHorizontalHeaderLabels({ QStringLiteral("ID"), QStringLiteral("Device"), QStringLiteral("Image"), QStringLiteral("Access") });
    m_impl->scsi->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    m_impl->scsi->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_impl->scsi->setSelectionMode(QAbstractItemView::SingleSelection);
    m_impl->scsi->setEditTriggers(QAbstractItemView::NoEditTriggers);
    scsiLayout->addWidget(m_impl->scsi, 1);
    auto* scsiButtons = new QHBoxLayout;
    auto* addDisk = new QPushButton(QStringLiteral("Add Hard Disk..."));
    auto* addCd = new QPushButton(QStringLiteral("Add CD-ROM..."));
    auto* remove = new QPushButton(QStringLiteral("Remove"));
    auto* create = new QPushButton(QStringLiteral("Create Disk Image..."));
    scsiButtons->addWidget(addDisk);
    scsiButtons->addWidget(addCd);
    scsiButtons->addWidget(remove);
    scsiButtons->addWidget(create);
    scsiButtons->addStretch();
    scsiLayout->addLayout(scsiButtons);
    m_impl->tabs->addTab(m_impl->scsiTab, QStringLiteral("SCSI"));

    m_impl->nubusTab = new QWidget;
    auto* nubusLayout = new QVBoxLayout(m_impl->nubusTab);
    m_impl->nubus = new NonEditableTableWidget(0, 2);
    m_impl->nubus->setHorizontalHeaderLabels({ QStringLiteral("Slot"), QStringLiteral("Card") });
    m_impl->nubus->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_impl->nubus->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_impl->nubus->setSelectionMode(QAbstractItemView::SingleSelection);
    m_impl->nubus->setEditTriggers(QAbstractItemView::NoEditTriggers);
    nubusLayout->addWidget(m_impl->nubus, 1);
    auto* nubusButtons = new QHBoxLayout;
    auto* addNuBus = new QPushButton(QStringLiteral("Add Card..."));
    auto* addNuBusMenu = new QMenu(addNuBus);
    auto* addCuteMacVideo = addNuBusMenu->addAction(QStringLiteral("CuteMac Video"));
    auto* addCuteMacAcceleratedVideo = addNuBusMenu->addAction(QStringLiteral("CuteMac Video Accelerated"));
    auto* addAppleVideo = addNuBusMenu->addAction(QStringLiteral("Apple Macintosh II Video Card"));
    addNuBus->setMenu(addNuBusMenu);
    auto* removeNuBus = new QPushButton(QStringLiteral("Remove"));
    nubusButtons->addWidget(addNuBus);
    nubusButtons->addWidget(removeNuBus);
    nubusButtons->addStretch();
    nubusLayout->addLayout(nubusButtons);
    m_impl->tabs->addTab(m_impl->nubusTab, QStringLiteral("NuBus"));

    const auto addScsiRow = [this](const config::ScsiDeviceConfiguration& device) {
        const int row = m_impl->scsi->rowCount();
        m_impl->scsi->insertRow(row);
        auto* id = new QComboBox;
        for (int value = 0; value <= 6; ++value) id->addItem(QString::number(value), value);
        id->setCurrentIndex(device.id);
        m_impl->scsi->setCellWidget(row, 0, id);
        auto* type = new QComboBox;
        type->addItem(QStringLiteral("Hard disk"), static_cast<int>(config::ScsiDeviceType::HardDisk));
        type->addItem(QStringLiteral("CD-ROM"), static_cast<int>(config::ScsiDeviceType::CdRom));
        type->setCurrentIndex(type->findData(static_cast<int>(device.type)));
        m_impl->scsi->setCellWidget(row, 1, type);
        m_impl->scsi->setItem(row, 2, new QTableWidgetItem(device.imagePath));
        auto* access = new QComboBox;
        access->addItem(QStringLiteral("Read/write"), false);
        access->addItem(QStringLiteral("Read-only"), true);
        access->setCurrentIndex(device.readOnly ? 1 : 0);
        m_impl->scsi->setCellWidget(row, 3, access);
    };
    const auto firstAvailableScsiId = [this](int preferred) {
        QSet<int> occupied;
        for (int row = 0; row < m_impl->scsi->rowCount(); ++row) {
            const auto* selector = qobject_cast<QComboBox*>(m_impl->scsi->cellWidget(row, 0));
            if (selector != nullptr) occupied.insert(selector->currentData().toInt());
        }
        for (int offset = 0; offset < 7; ++offset) {
            const auto id = (preferred + offset) % 7;
            if (!occupied.contains(id)) return id;
        }
        return -1;
    };
    for (const auto& device : m_impl->original.scsiDevices) addScsiRow(device);

    m_impl->nubusDevices = m_impl->original.nubusDevices;
    const auto refreshNuBus = [this]() {
        m_impl->nubus->setRowCount(0);
        for (const auto& device : m_impl->nubusDevices) {
            const int row = m_impl->nubus->rowCount();
            m_impl->nubus->insertRow(row);
            m_impl->nubus->setItem(row, 0, new QTableWidgetItem(QString::number(device.slot)));
            m_impl->nubus->setItem(row, 1, new QTableWidgetItem(nubusCardName(device.type)));
        }
    };
    refreshNuBus();

    const auto updateCapabilities = [this]() {
        const auto machineId = m_impl->machine->currentData().toString();
        const auto profiles = machines::MachineCatalog::supportedMachines();
        const auto it = std::find_if(profiles.cbegin(), profiles.cend(), [&](const auto& profile) { return profile.id == machineId; });
        const auto devices = it == profiles.cend() ? QStringList {} : it->reusableDevices;
        const bool iwm = devices.contains(QStringLiteral("device.iwm")) || devices.contains(QStringLiteral("device.swim1"));
        const bool scsi = devices.contains(QStringLiteral("device.scsi.ncr5380")) || devices.contains(QStringLiteral("device.scsi.bus"));
        const bool nubus = devices.contains(QStringLiteral("device.nubus"));
        const bool serial = std::any_of(devices.cbegin(), devices.cend(),
            [](const QString& device) { return device.startsWith(QStringLiteral("device.scc")); });
        m_impl->tabs->setTabVisible(m_impl->tabs->indexOf(m_impl->iwmTab), iwm);
        m_impl->tabs->setTabVisible(m_impl->tabs->indexOf(m_impl->scsiTab), scsi);
        m_impl->tabs->setTabVisible(m_impl->tabs->indexOf(m_impl->nubusTab), nubus);
        m_impl->tabs->setTabVisible(m_impl->tabs->indexOf(m_impl->serialTab), serial);
        m_impl->ram->clear();
        if (it != profiles.cend()) {
            for (const auto sizeKiB : it->supportedRamSizesKiB) {
                const auto label = sizeKiB < 1024 ? QStringLiteral("%1 KiB").arg(sizeKiB)
                    : sizeKiB % 1024 == 0 ? QStringLiteral("%1 MiB").arg(sizeKiB / 1024)
                                           : QStringLiteral("%1 MiB").arg(sizeKiB / 1024.0, 0, 'f', 1);
                m_impl->ram->addItem(label, sizeKiB);
            }
        }
        auto ramIndex = m_impl->ram->findData(m_impl->original.ramSizeKiB);
        if (ramIndex < 0) ramIndex = 0;
        m_impl->ram->setCurrentIndex(ramIndex);
    };

    connect(m_impl->machine, &QComboBox::currentIndexChanged, this, updateCapabilities);
    connect(nvramBrowse, &QPushButton::clicked, this, [this]() {
        const auto path = QFileDialog::getOpenFileName(this, QStringLiteral("Select NVRAM Image"),
            config::ConfigurationManager::diskImageDirectoryPath(), QStringLiteral("NVRAM images (*.nvram *.pram);;All files (*)"));
        if (!path.isEmpty()) m_impl->nvram->setText(path);
    });
    connect(nvramNew, &QPushButton::clicked, this, [this]() {
        const auto path = QFileDialog::getSaveFileName(this, QStringLiteral("Create NVRAM Image"),
            config::ConfigurationManager::diskImageDirectoryPath(), QStringLiteral("NVRAM image (*.nvram)"));
        if (!path.isEmpty()) m_impl->nvram->setText(path);
    });
    connect(nvramZap, &QPushButton::clicked, this, [this]() {
        const auto path = m_impl->nvram->text().trimmed();
        if (path.isEmpty()) {
            QMessageBox::information(this, QStringLiteral("Zap NVRAM"),
                QStringLiteral("Select or create an NVRAM image first."));
            return;
        }
        if (QMessageBox::warning(this, QStringLiteral("Zap NVRAM"),
                QStringLiteral("Erase all parameter RAM settings in this image?\n\n%1\n\n"
                               "The Macintosh ROM will install its default settings on the next reset.")
                    .arg(path),
                QMessageBox::Yes | QMessageBox::No, QMessageBox::No)
            != QMessageBox::Yes) {
            return;
        }

        QSaveFile file(path);
        const QByteArray blankNvram(256, '\0');
        if (!file.open(QIODevice::WriteOnly)
            || file.write(blankNvram) != blankNvram.size()
            || !file.commit()) {
            QMessageBox::critical(this, QStringLiteral("Zap NVRAM"),
                QStringLiteral("Could not erase the NVRAM image."));
            return;
        }
        m_impl->nvramZapped = true;
        QMessageBox::information(this, QStringLiteral("Zap NVRAM"),
            QStringLiteral("The NVRAM image was erased. Apply the configuration to reset the Macintosh."));
    });
    connect(addDisk, &QPushButton::clicked, this, [this, addScsiRow, firstAvailableScsiId]() {
        const auto path = DiskImagePickerDialog::getImage(storage::DiskImageType::HardDisk, QStringLiteral("Select Hard Disk Image"), this);
        if (path.isEmpty()) return;
        const auto id = firstAvailableScsiId(0);
        if (id < 0) {
            QMessageBox::warning(this, windowTitle(), QStringLiteral("All SCSI IDs are already in use."));
            return;
        }
        addScsiRow({ id, config::ScsiDeviceType::HardDisk, path, false });
    });
    connect(addCd, &QPushButton::clicked, this, [this, addScsiRow, firstAvailableScsiId]() {
        const auto path = DiskImagePickerDialog::getImage(storage::DiskImageType::CdRom, QStringLiteral("Select CD-ROM Image"), this);
        if (path.isEmpty()) return;
        const auto id = firstAvailableScsiId(3);
        if (id < 0) {
            QMessageBox::warning(this, windowTitle(), QStringLiteral("All SCSI IDs are already in use."));
            return;
        }
        addScsiRow({ id, config::ScsiDeviceType::CdRom, path, true });
    });
    connect(remove, &QPushButton::clicked, this, [this]() {
        if (m_impl->scsi->currentRow() >= 0) m_impl->scsi->removeRow(m_impl->scsi->currentRow());
    });
    connect(create, &QPushButton::clicked, this, [this, addScsiRow, firstAvailableScsiId]() {
        const auto path = createDiskImage(storage::DiskImageType::HardDisk, this);
        if (path.isEmpty()) return;
        const auto id = firstAvailableScsiId(0);
        if (id < 0) {
            QMessageBox::warning(this, windowTitle(), QStringLiteral("All SCSI IDs are already in use."));
            return;
        }
        addScsiRow({ id, config::ScsiDeviceType::HardDisk, path, false });
    });
    connect(addCuteMacVideo, &QAction::triggered, this, [this, refreshNuBus]() {
        config::NuBusDeviceConfiguration device {9 + static_cast<int>(m_impl->nubusDevices.size() % 3), config::NuBusDeviceType::CuteMacVideo, {}, 640, 480, 8, 4096, true};
        if (editNuBusCard(device, this)) {
            m_impl->nubusDevices.append(device);
            refreshNuBus();
        }
    });
    connect(addCuteMacAcceleratedVideo, &QAction::triggered, this, [this, refreshNuBus]() {
        config::NuBusDeviceConfiguration device {9 + static_cast<int>(m_impl->nubusDevices.size() % 3),
            config::NuBusDeviceType::CuteMacVideoAccelerated, {}, 640, 480, 8, 4096, true};
        if (editNuBusCard(device, this)) {
            m_impl->nubusDevices.append(device);
            refreshNuBus();
        }
    });
    connect(addAppleVideo, &QAction::triggered, this, [this, refreshNuBus]() {
        config::NuBusDeviceConfiguration device {9 + static_cast<int>(m_impl->nubusDevices.size() % 3), config::NuBusDeviceType::MacintoshIIVideo, {}, 640, 480, 1, 512, false};
        if (editNuBusCard(device, this)) {
            m_impl->nubusDevices.append(device);
            refreshNuBus();
        }
    });
    connect(removeNuBus, &QPushButton::clicked, this, [this, refreshNuBus]() {
        const int row = m_impl->nubus->currentRow();
        if (row >= 0) {
            m_impl->nubusDevices.removeAt(row);
            refreshNuBus();
        }
    });
    connect(m_impl->nubus, &QTableWidget::cellDoubleClicked, this, [this, refreshNuBus](int row, int) {
        if (row >= 0 && row < m_impl->nubusDevices.size() && editNuBusCard(m_impl->nubusDevices[row], this)) refreshNuBus();
    });

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    outer->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(buttons, &QDialogButtonBox::accepted, this, [this]() {
        if (m_impl->name->text().trimmed().isEmpty()) {
            QMessageBox::warning(this, windowTitle(), QStringLiteral("Profile name is required."));
            return;
        }
        for (int channel = 0; channel < 2; ++channel) {
            if (m_impl->serialDevice[channel]->currentData().toInt() < 0) continue;
            auto serial = m_impl->serialConfiguration[channel];
            serial.channel = channel;
            serial.type = static_cast<config::SerialDeviceType>(m_impl->serialDevice[channel]->currentData().toInt());
            if (!config::isValidSerialDeviceConfiguration(serial)) {
                QMessageBox::warning(this, windowTitle(),
                    QStringLiteral("Configure the %1 attached to the %2 port.")
                        .arg(serialDeviceName(serial.type),
                            channel == 0 ? QStringLiteral("modem") : QStringLiteral("printer")));
                return;
            }
        }
        QSet<int> ids;
        for (int row = 0; row < m_impl->scsi->rowCount(); ++row) {
            const int id = qobject_cast<QComboBox*>(m_impl->scsi->cellWidget(row, 0))->currentData().toInt();
            if (ids.contains(id)) {
                QMessageBox::warning(this, windowTitle(), QStringLiteral("Each SCSI ID can only be used once."));
                return;
            }
            ids.insert(id);
        }
        QSet<int> occupiedSlots;
        for (const auto& device : m_impl->nubusDevices) {
            const int slot = device.slot;
            if (occupiedSlots.contains(slot)) {
                QMessageBox::warning(this, windowTitle(), QStringLiteral("Each NuBus slot can only be used once."));
                return;
            }
            const auto validation = nubusValidationMessage(device);
            if (!validation.isEmpty()) {
                QMessageBox::warning(this, windowTitle(), validation);
                return;
            }
            occupiedSlots.insert(slot);
        }
        auto prospective = this->configuration();
        const auto romWarning = rom::RomCatalog().warningForConfiguration(prospective);
        if (!romWarning.isEmpty()) {
            QMessageBox::warning(this, QStringLiteral("ROMs Missing or Discouraged"), romWarning
                + QStringLiteral("\n\nYou can still save this profile. Add ROMs using Tools → ROM Manager."));
        }
        accept();
    });
    updateCapabilities();
}

ConfigurationDialog::~ConfigurationDialog() = default;

bool ConfigurationDialog::nvramZapped() const
{
    return m_impl->nvramZapped;
}

config::Configuration ConfigurationDialog::configuration() const
{
    auto result = m_impl->original;
    result.profileName = m_impl->name->text().trimmed();
    result.machineId = m_impl->machine->currentData().toString();
    result.romPath.clear();
    result.nvramPath = m_impl->nvram->text().trimmed();
    result.ramSizeKiB = m_impl->ram->currentData().toInt();
    result.runtimeSpeed = static_cast<config::RuntimeSpeed>(m_impl->speed->currentData().toInt());
    result.skipRamPatternTest = m_impl->skipRamTest->isChecked();
    result.serialDevices.clear();
    for (int channel = 0; channel < 2; ++channel) {
        if (m_impl->serialDevice[channel]->currentData().toInt() >= 0) {
            auto serial = m_impl->serialConfiguration[channel];
            serial.channel = channel;
            serial.type = static_cast<config::SerialDeviceType>(m_impl->serialDevice[channel]->currentData().toInt());
            if (serial.type == config::SerialDeviceType::HayesModem && serial.phonebook.isEmpty()) {
                serial.phonebook = config::defaultSerialModemPhonebook();
            }
            result.serialDevices.append(serial);
        }
    }
    const auto machineProfile = machines::MachineCatalog::find(result.machineId);
    const auto machineDevices = machineProfile ? machineProfile->reusableDevices : QStringList {};
    if (machineDevices.contains(QStringLiteral("device.iwm")) || machineDevices.contains(QStringLiteral("device.swim1"))) {
        result.iwmDevices = {
            { m_impl->floppy[0]->text().trimmed(), m_impl->floppyReadOnly[0]->isChecked() },
            { m_impl->floppy[1]->text().trimmed(), m_impl->floppyReadOnly[1]->isChecked() },
        };
        result.floppyPath = result.iwmDevices.first().imagePath;
    } else {
        result.iwmDevices.clear();
        result.floppyPath.clear();
    }
    result.scsiDevices.clear();
    for (int row = 0; row < m_impl->scsi->rowCount(); ++row) {
        result.scsiDevices.append({
            qobject_cast<QComboBox*>(m_impl->scsi->cellWidget(row, 0))->currentData().toInt(),
            static_cast<config::ScsiDeviceType>(qobject_cast<QComboBox*>(m_impl->scsi->cellWidget(row, 1))->currentData().toInt()),
            m_impl->scsi->item(row, 2)->text(),
            qobject_cast<QComboBox*>(m_impl->scsi->cellWidget(row, 3))->currentData().toBool(),
        });
    }
    result.nubusDevices = m_impl->nubusDevices;
    for (auto& device : result.nubusDevices) device.declarationRomPath.clear();
    result.diskPath = result.scsiDevices.isEmpty() ? QString() : result.scsiDevices.first().imagePath;
    return result;
}

} // namespace cutemac::ui
