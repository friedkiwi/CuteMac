#include "cutemac/ui/ConfigurationDialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QTabWidget>
#include <QTableWidget>
#include <QVBoxLayout>
#include <QSet>

#include <algorithm>

#include "cutemac/machines/MachineCatalog.h"
#include "cutemac/ui/DiskImageWidgets.h"

namespace cutemac::ui {

class ConfigurationDialog::Impl {
public:
    config::Configuration original;
    QLineEdit* name = nullptr;
    QComboBox* machine = nullptr;
    QLineEdit* rom = nullptr;
    QLineEdit* nvram = nullptr;
    QSpinBox* ram = nullptr;
    QComboBox* speed = nullptr;
    QCheckBox* skipRamTest = nullptr;
    QTabWidget* tabs = nullptr;
    QWidget* iwmTab = nullptr;
    QLineEdit* floppy = nullptr;
    QCheckBox* floppyReadOnly = nullptr;
    QWidget* scsiTab = nullptr;
    QTableWidget* scsi = nullptr;
    QWidget* nubusTab = nullptr;
    QTableWidget* nubus = nullptr;
};

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

    m_impl->rom = new QLineEdit(m_impl->original.romPath);
    auto* romBrowse = new QPushButton(QStringLiteral("Browse..."));
    auto* romRow = new QHBoxLayout;
    romRow->addWidget(m_impl->rom, 1);
    romRow->addWidget(romBrowse);
    form->addRow(QStringLiteral("ROM"), romRow);
    m_impl->nvram = new QLineEdit(m_impl->original.nvramPath);
    auto* nvramBrowse = new QPushButton(QStringLiteral("Browse..."));
    auto* nvramNew = new QPushButton(QStringLiteral("New..."));
    auto* nvramRow = new QHBoxLayout;
    nvramRow->addWidget(m_impl->nvram, 1);
    nvramRow->addWidget(nvramBrowse);
    nvramRow->addWidget(nvramNew);
    form->addRow(QStringLiteral("NVRAM image"), nvramRow);
    m_impl->ram = new QSpinBox;
    m_impl->ram->setSuffix(QStringLiteral(" MiB"));
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

    m_impl->iwmTab = new QWidget;
    auto* iwmForm = new QFormLayout(m_impl->iwmTab);
    m_impl->floppy = new QLineEdit(m_impl->original.iwmDevices.isEmpty() ? m_impl->original.floppyPath : m_impl->original.iwmDevices.first().imagePath);
    auto* floppyBrowse = new QPushButton(QStringLiteral("Browse..."));
    auto* floppyNew = new QPushButton(QStringLiteral("New..."));
    auto* floppyRow = new QHBoxLayout;
    floppyRow->addWidget(m_impl->floppy, 1);
    floppyRow->addWidget(floppyBrowse);
    floppyRow->addWidget(floppyNew);
    iwmForm->addRow(QStringLiteral("Internal floppy"), floppyRow);
    m_impl->floppyReadOnly = new QCheckBox;
    if (!m_impl->original.iwmDevices.isEmpty()) m_impl->floppyReadOnly->setChecked(m_impl->original.iwmDevices.first().readOnly);
    iwmForm->addRow(QStringLiteral("Read-only"), m_impl->floppyReadOnly);
    m_impl->tabs->addTab(m_impl->iwmTab, QStringLiteral("IWM"));

    m_impl->scsiTab = new QWidget;
    auto* scsiLayout = new QVBoxLayout(m_impl->scsiTab);
    m_impl->scsi = new QTableWidget(0, 4);
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
    m_impl->nubus = new QTableWidget(0, 8);
    m_impl->nubus->setHorizontalHeaderLabels({ QStringLiteral("Slot"), QStringLiteral("Card"), QStringLiteral("Declaration ROM"),
        QStringLiteral("Width"), QStringLiteral("Height"), QStringLiteral("Depth"), QStringLiteral("VRAM MiB"), QStringLiteral("Acceleration") });
    m_impl->nubus->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    m_impl->nubus->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_impl->nubus->setSelectionMode(QAbstractItemView::SingleSelection);
    nubusLayout->addWidget(m_impl->nubus, 1);
    auto* nubusButtons = new QHBoxLayout;
    auto* addCuteMacVideo = new QPushButton(QStringLiteral("Add CuteMac Video"));
    auto* addAppleVideo = new QPushButton(QStringLiteral("Add Macintosh II Video Card..."));
    auto* removeNuBus = new QPushButton(QStringLiteral("Remove"));
    nubusButtons->addWidget(addCuteMacVideo);
    nubusButtons->addWidget(addAppleVideo);
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
    for (const auto& device : m_impl->original.scsiDevices) addScsiRow(device);

    const auto addNuBusRow = [this](const config::NuBusDeviceConfiguration& device) {
        const int row = m_impl->nubus->rowCount();
        m_impl->nubus->insertRow(row);
        auto* slot = new QComboBox;
        for (int value = 9; value <= 11; ++value) slot->addItem(QString::number(value), value);
        slot->setCurrentIndex(qMax(0, slot->findData(device.slot)));
        m_impl->nubus->setCellWidget(row, 0, slot);
        auto* type = new QComboBox;
        type->addItem(QStringLiteral("CuteMac Video"), static_cast<int>(config::NuBusDeviceType::CuteMacVideo));
        type->addItem(QStringLiteral("Apple 630-0153"), static_cast<int>(config::NuBusDeviceType::MacintoshIIVideo));
        type->setCurrentIndex(type->findData(static_cast<int>(device.type)));
        m_impl->nubus->setCellWidget(row, 1, type);
        m_impl->nubus->setItem(row, 2, new QTableWidgetItem(device.declarationRomPath));
        const auto spin = [this, row](int column, int minimum, int maximum, int value) {
            auto* widget = new QSpinBox;
            widget->setRange(minimum, maximum);
            widget->setValue(value);
            m_impl->nubus->setCellWidget(row, column, widget);
        };
        spin(3, 320, 4096, device.width);
        spin(4, 200, 2160, device.height);
        auto* depth = new QComboBox;
        for (const int value : {1, 2, 4, 8, 16, 32}) depth->addItem(QString::number(value), value);
        depth->setCurrentIndex(qMax(0, depth->findData(device.depth)));
        m_impl->nubus->setCellWidget(row, 5, depth);
        spin(6, 1, 14, device.vramMiB);
        auto* acceleration = new QCheckBox;
        acceleration->setChecked(device.acceleration);
        acceleration->setEnabled(device.type == config::NuBusDeviceType::CuteMacVideo);
        m_impl->nubus->setCellWidget(row, 7, acceleration);
    };
    for (const auto& device : m_impl->original.nubusDevices) addNuBusRow(device);

    const auto updateCapabilities = [this]() {
        const auto machineId = m_impl->machine->currentData().toString();
        const auto profiles = machines::MachineCatalog::supportedMachines();
        const auto it = std::find_if(profiles.cbegin(), profiles.cend(), [&](const auto& profile) { return profile.id == machineId; });
        const auto devices = it == profiles.cend() ? QStringList {} : it->reusableDevices;
        const bool iwm = devices.contains(QStringLiteral("device.iwm")) || devices.contains(QStringLiteral("device.swim1"));
        const bool scsi = devices.contains(QStringLiteral("device.scsi.ncr5380")) || devices.contains(QStringLiteral("device.scsi.bus"));
        const bool nubus = devices.contains(QStringLiteral("device.nubus"));
        m_impl->tabs->setTabVisible(m_impl->tabs->indexOf(m_impl->iwmTab), iwm);
        m_impl->tabs->setTabVisible(m_impl->tabs->indexOf(m_impl->scsiTab), scsi);
        m_impl->tabs->setTabVisible(m_impl->tabs->indexOf(m_impl->nubusTab), nubus);
        m_impl->ram->setRange(machineId == QStringLiteral("mac-plus") ? 1 : 1, machineId == QStringLiteral("mac-plus") ? 4 : 256);
        m_impl->ram->setValue(qBound(m_impl->ram->minimum(), m_impl->original.ramSizeMiB, m_impl->ram->maximum()));
    };

    connect(m_impl->machine, &QComboBox::currentIndexChanged, this, updateCapabilities);
    connect(romBrowse, &QPushButton::clicked, this, [this]() {
        const auto path = QFileDialog::getOpenFileName(this, QStringLiteral("Select ROM"), config::ConfigurationManager::romDirectoryPath(), QStringLiteral("ROM images (*.rom *.bin);;All files (*)"));
        if (!path.isEmpty()) m_impl->rom->setText(path);
    });
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
    connect(floppyBrowse, &QPushButton::clicked, this, [this]() {
        const auto path = DiskImagePickerDialog::getImage(storage::DiskImageType::Floppy, QStringLiteral("Select Floppy Image"), this);
        if (!path.isEmpty()) m_impl->floppy->setText(path);
    });
    connect(floppyNew, &QPushButton::clicked, this, [this]() {
        const auto path = createDiskImage(storage::DiskImageType::Floppy, this);
        if (!path.isEmpty()) m_impl->floppy->setText(path);
    });
    connect(addDisk, &QPushButton::clicked, this, [this, addScsiRow]() {
        const auto path = DiskImagePickerDialog::getImage(storage::DiskImageType::HardDisk, QStringLiteral("Select Hard Disk Image"), this);
        if (!path.isEmpty()) addScsiRow({ m_impl->scsi->rowCount() % 7, config::ScsiDeviceType::HardDisk, path, false });
    });
    connect(addCd, &QPushButton::clicked, this, [this, addScsiRow]() {
        const auto path = DiskImagePickerDialog::getImage(storage::DiskImageType::CdRom, QStringLiteral("Select CD-ROM Image"), this);
        if (!path.isEmpty()) addScsiRow({ m_impl->scsi->rowCount() % 7, config::ScsiDeviceType::CdRom, path, true });
    });
    connect(remove, &QPushButton::clicked, this, [this]() {
        if (m_impl->scsi->currentRow() >= 0) m_impl->scsi->removeRow(m_impl->scsi->currentRow());
    });
    connect(create, &QPushButton::clicked, this, [this, addScsiRow]() {
        const auto path = createDiskImage(storage::DiskImageType::HardDisk, this);
        if (!path.isEmpty()) addScsiRow({ m_impl->scsi->rowCount() % 7, config::ScsiDeviceType::HardDisk, path, false });
    });
    connect(addCuteMacVideo, &QPushButton::clicked, this, [this, addNuBusRow]() {
        addNuBusRow({9 + (m_impl->nubus->rowCount() % 3), config::NuBusDeviceType::CuteMacVideo, {}, 640, 480, 8, 4, true});
    });
    connect(addAppleVideo, &QPushButton::clicked, this, [this, addNuBusRow]() {
        const auto path = QFileDialog::getOpenFileName(this, QStringLiteral("Select Apple 342-0008-A Declaration ROM"),
            config::ConfigurationManager::romDirectoryPath(), QStringLiteral("ROM images (*.rom *.bin);;All files (*)"));
        if (!path.isEmpty()) addNuBusRow({9 + (m_impl->nubus->rowCount() % 3), config::NuBusDeviceType::MacintoshIIVideo, path, 640, 480, 1, 1, false});
    });
    connect(removeNuBus, &QPushButton::clicked, this, [this]() {
        if (m_impl->nubus->currentRow() >= 0) m_impl->nubus->removeRow(m_impl->nubus->currentRow());
    });

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    outer->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(buttons, &QDialogButtonBox::accepted, this, [this]() {
        if (m_impl->name->text().trimmed().isEmpty()) {
            QMessageBox::warning(this, windowTitle(), QStringLiteral("Profile name is required."));
            return;
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
        for (int row = 0; row < m_impl->nubus->rowCount(); ++row) {
            const int slot = qobject_cast<QComboBox*>(m_impl->nubus->cellWidget(row, 0))->currentData().toInt();
            if (occupiedSlots.contains(slot)) {
                QMessageBox::warning(this, windowTitle(), QStringLiteral("Each NuBus slot can only be used once."));
                return;
            }
            occupiedSlots.insert(slot);
            const auto type = static_cast<config::NuBusDeviceType>(qobject_cast<QComboBox*>(m_impl->nubus->cellWidget(row, 1))->currentData().toInt());
            if (type == config::NuBusDeviceType::MacintoshIIVideo && m_impl->nubus->item(row, 2)->text().trimmed().isEmpty()) {
                QMessageBox::warning(this, windowTitle(), QStringLiteral("The Apple 630-0153 card requires its 342-0008-A declaration ROM."));
                return;
            }
        }
        accept();
    });
    updateCapabilities();
}

ConfigurationDialog::~ConfigurationDialog() = default;

config::Configuration ConfigurationDialog::configuration() const
{
    auto result = m_impl->original;
    result.profileName = m_impl->name->text().trimmed();
    result.machineId = m_impl->machine->currentData().toString();
    result.romPath = m_impl->rom->text().trimmed();
    result.nvramPath = m_impl->nvram->text().trimmed();
    result.ramSizeMiB = m_impl->ram->value();
    result.runtimeSpeed = static_cast<config::RuntimeSpeed>(m_impl->speed->currentData().toInt());
    result.skipRamPatternTest = m_impl->skipRamTest->isChecked();
    result.iwmDevices = { { m_impl->floppy->text().trimmed(), m_impl->floppyReadOnly->isChecked() } };
    result.floppyPath = result.iwmDevices.first().imagePath;
    result.scsiDevices.clear();
    for (int row = 0; row < m_impl->scsi->rowCount(); ++row) {
        result.scsiDevices.append({
            qobject_cast<QComboBox*>(m_impl->scsi->cellWidget(row, 0))->currentData().toInt(),
            static_cast<config::ScsiDeviceType>(qobject_cast<QComboBox*>(m_impl->scsi->cellWidget(row, 1))->currentData().toInt()),
            m_impl->scsi->item(row, 2)->text(),
            qobject_cast<QComboBox*>(m_impl->scsi->cellWidget(row, 3))->currentData().toBool(),
        });
    }
    result.nubusDevices.clear();
    for (int row = 0; row < m_impl->nubus->rowCount(); ++row) {
        result.nubusDevices.append({
            qobject_cast<QComboBox*>(m_impl->nubus->cellWidget(row, 0))->currentData().toInt(),
            static_cast<config::NuBusDeviceType>(qobject_cast<QComboBox*>(m_impl->nubus->cellWidget(row, 1))->currentData().toInt()),
            m_impl->nubus->item(row, 2)->text().trimmed(),
            qobject_cast<QSpinBox*>(m_impl->nubus->cellWidget(row, 3))->value(),
            qobject_cast<QSpinBox*>(m_impl->nubus->cellWidget(row, 4))->value(),
            qobject_cast<QComboBox*>(m_impl->nubus->cellWidget(row, 5))->currentData().toInt(),
            qobject_cast<QSpinBox*>(m_impl->nubus->cellWidget(row, 6))->value(),
            qobject_cast<QCheckBox*>(m_impl->nubus->cellWidget(row, 7))->isChecked(),
        });
    }
    result.diskPath = result.scsiDevices.isEmpty() ? QString() : result.scsiDevices.first().imagePath;
    return result;
}

} // namespace cutemac::ui
