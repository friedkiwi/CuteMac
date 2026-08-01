#include "cutemac/ui/ConfigurationDialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFile>
#include <QFileDialog>
#include <QFormLayout>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QDoubleValidator>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QRegularExpression>
#include <QSpinBox>
#include <QTabWidget>
#include <QTableWidget>
#include <QVBoxLayout>
#include <QSet>

#include <algorithm>

#include "cutemac/machines/MachineCatalog.h"

namespace cutemac::ui {
namespace {

QString chooseImage(QWidget* parent, const QString& title, const QString& filter)
{
    return QFileDialog::getOpenFileName(parent, title, config::ConfigurationManager::diskImageDirectoryPath(), filter);
}

class DiskImageDialog final : public QDialog {
public:
    explicit DiskImageDialog(QWidget* parent = nullptr)
        : QDialog(parent)
    {
        setWindowTitle(QStringLiteral("Create Blank Disk Image"));
        auto* layout = new QVBoxLayout(this);
        auto* form = new QFormLayout;
        m_path = new QLineEdit;
        auto* browse = new QPushButton(QStringLiteral("Browse..."));
        auto* pathRow = new QHBoxLayout;
        pathRow->addWidget(m_path, 1);
        pathRow->addWidget(browse);
        form->addRow(QStringLiteral("Image file"), pathRow);

        m_preset = new QComboBox;
        m_preset->addItem(QStringLiteral("20 MB (early external drive)"), 20);
        m_preset->addItem(QStringLiteral("40 MB (Macintosh IIcx)"), 40);
        m_preset->addItem(QStringLiteral("80 MB (Macintosh IIcx)"), 80);
        m_preset->addItem(QStringLiteral("160 MB"), 160);
        m_preset->addItem(QStringLiteral("230 MB (Quadra 800)"), 230);
        m_preset->addItem(QStringLiteral("500 MB (Quadra 800)"), 500);
        m_preset->addItem(QStringLiteral("1 GB (Quadra 800)"), 1024);
        m_preset->addItem(QStringLiteral("Custom"), -1);
        form->addRow(QStringLiteral("Common size"), m_preset);

        m_custom = new QLineEdit(QStringLiteral("100"));
        auto* sizeValidator = new QDoubleValidator(0.001, 999999.0, 3, m_custom);
        sizeValidator->setNotation(QDoubleValidator::StandardNotation);
        m_custom->setValidator(sizeValidator);
        m_unit = new QComboBox;
        m_unit->addItem(QStringLiteral("MB"), 1024LL * 1024LL);
        m_unit->addItem(QStringLiteral("GB"), 1024LL * 1024LL * 1024LL);
        m_lastUnitMultiplier = m_unit->currentData().toLongLong();
        auto* customRow = new QHBoxLayout;
        customRow->addWidget(m_custom, 1);
        customRow->addWidget(m_unit);
        form->addRow(QStringLiteral("Custom size"), customRow);
        layout->addLayout(form);

        auto* note = new QLabel(QStringLiteral("Images are created as raw, sparse files. They are not partitioned or formatted."));
        note->setWordWrap(true);
        layout->addWidget(note);
        auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
        layout->addWidget(buttons);

        const auto updateCustom = [this]() {
            const bool custom = m_preset->currentData().toInt() < 0;
            m_custom->setEnabled(custom);
            m_unit->setEnabled(custom);
        };
        connect(m_preset, &QComboBox::currentIndexChanged, this, updateCustom);
        connect(m_unit, &QComboBox::currentIndexChanged, this, [this]() {
            const auto bytes = m_custom->text().toDouble() * static_cast<double>(m_lastUnitMultiplier);
            m_lastUnitMultiplier = m_unit->currentData().toLongLong();
            m_custom->setText(QString::number(bytes / static_cast<double>(m_lastUnitMultiplier), 'f', 3).remove(QRegularExpression(QStringLiteral(R"(\.?0+$)"))));
        });
        connect(browse, &QPushButton::clicked, this, [this]() {
            const auto path = QFileDialog::getSaveFileName(this, QStringLiteral("Create Blank Disk Image"),
                config::ConfigurationManager::diskImageDirectoryPath(), QStringLiteral("Raw disk image (*.hda *.img);;All files (*)"));
            if (!path.isEmpty()) m_path->setText(path);
        });
        connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
        connect(buttons, &QDialogButtonBox::accepted, this, [this]() {
            if (m_path->text().trimmed().isEmpty()) {
                QMessageBox::warning(this, windowTitle(), QStringLiteral("Select an image file."));
                return;
            }
            const auto bytes = sizeBytes();
            if (bytes > 4LL * 1024 * 1024 * 1024
                && QMessageBox::warning(this, windowTitle(), QStringLiteral("Images over 4 GB may not work with vintage Macintosh software. Create it anyway?"),
                       QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes) {
                return;
            }
            QFile file(m_path->text().trimmed());
            if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate) || !file.resize(bytes)) {
                QMessageBox::critical(this, windowTitle(), QStringLiteral("Could not create the disk image."));
                return;
            }
            accept();
        });
        updateCustom();
    }

    [[nodiscard]] QString imagePath() const { return m_path->text().trimmed(); }

private:
    [[nodiscard]] qint64 sizeBytes() const
    {
        const auto presetMiB = m_preset->currentData().toLongLong();
        return presetMiB >= 0 ? presetMiB * 1024 * 1024 : qRound64(m_custom->text().toDouble() * m_unit->currentData().toLongLong());
    }

    QLineEdit* m_path = nullptr;
    QComboBox* m_preset = nullptr;
    QLineEdit* m_custom = nullptr;
    QComboBox* m_unit = nullptr;
    qint64 m_lastUnitMultiplier = 1024LL * 1024LL;
};

} // namespace

class ConfigurationDialog::Impl {
public:
    config::Configuration original;
    QLineEdit* name = nullptr;
    QComboBox* machine = nullptr;
    QLineEdit* rom = nullptr;
    QSpinBox* ram = nullptr;
    QComboBox* speed = nullptr;
    QSpinBox* cycles = nullptr;
    QCheckBox* skipRamTest = nullptr;
    QTabWidget* tabs = nullptr;
    QWidget* iwmTab = nullptr;
    QLineEdit* floppy = nullptr;
    QCheckBox* floppyReadOnly = nullptr;
    QWidget* scsiTab = nullptr;
    QTableWidget* scsi = nullptr;
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
    m_impl->ram = new QSpinBox;
    m_impl->ram->setSuffix(QStringLiteral(" MiB"));
    form->addRow(QStringLiteral("RAM"), m_impl->ram);
    m_impl->speed = new QComboBox;
    m_impl->speed->addItem(QStringLiteral("Unlimited"), static_cast<int>(config::RuntimeSpeed::Unlimited));
    m_impl->speed->addItem(QStringLiteral("Realtime"), static_cast<int>(config::RuntimeSpeed::Realtime));
    m_impl->speed->setCurrentIndex(m_impl->speed->findData(static_cast<int>(m_impl->original.runtimeSpeed)));
    form->addRow(QStringLiteral("Emulation speed"), m_impl->speed);
    m_impl->cycles = new QSpinBox;
    m_impl->cycles->setRange(1000, 2000000);
    m_impl->cycles->setValue(m_impl->original.cyclesPerFrame);
    form->addRow(QStringLiteral("Cycles/frame"), m_impl->cycles);
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
    scsiLayout->addWidget(m_impl->scsi, 1);
    auto* scsiButtons = new QHBoxLayout;
    auto* addDisk = new QPushButton(QStringLiteral("Add Hard Disk..."));
    auto* remove = new QPushButton(QStringLiteral("Remove"));
    auto* create = new QPushButton(QStringLiteral("Create Disk Image..."));
    scsiButtons->addWidget(addDisk);
    scsiButtons->addWidget(remove);
    scsiButtons->addWidget(create);
    scsiButtons->addStretch();
    scsiLayout->addLayout(scsiButtons);
    m_impl->tabs->addTab(m_impl->scsiTab, QStringLiteral("SCSI"));

    const auto addScsiRow = [this](const config::ScsiDeviceConfiguration& device) {
        const int row = m_impl->scsi->rowCount();
        m_impl->scsi->insertRow(row);
        auto* id = new QComboBox;
        for (int value = 0; value <= 6; ++value) id->addItem(QString::number(value), value);
        id->setCurrentIndex(device.id);
        m_impl->scsi->setCellWidget(row, 0, id);
        auto* type = new QComboBox;
        type->addItem(QStringLiteral("Hard disk"), static_cast<int>(config::ScsiDeviceType::HardDisk));
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

    const auto updateCapabilities = [this]() {
        const auto machineId = m_impl->machine->currentData().toString();
        const auto profiles = machines::MachineCatalog::supportedMachines();
        const auto it = std::find_if(profiles.cbegin(), profiles.cend(), [&](const auto& profile) { return profile.id == machineId; });
        const auto devices = it == profiles.cend() ? QStringList {} : it->reusableDevices;
        const bool iwm = devices.contains(QStringLiteral("device.iwm"));
        const bool scsi = devices.contains(QStringLiteral("device.scsi.ncr5380")) || devices.contains(QStringLiteral("device.scsi.bus"));
        m_impl->tabs->setTabVisible(m_impl->tabs->indexOf(m_impl->iwmTab), iwm);
        m_impl->tabs->setTabVisible(m_impl->tabs->indexOf(m_impl->scsiTab), scsi);
        m_impl->ram->setRange(machineId == QStringLiteral("mac-plus") ? 1 : 1, machineId == QStringLiteral("mac-plus") ? 4 : 256);
        m_impl->ram->setValue(qBound(m_impl->ram->minimum(), m_impl->original.ramSizeMiB, m_impl->ram->maximum()));
    };

    connect(m_impl->machine, &QComboBox::currentIndexChanged, this, updateCapabilities);
    connect(romBrowse, &QPushButton::clicked, this, [this]() {
        const auto path = QFileDialog::getOpenFileName(this, QStringLiteral("Select ROM"), config::ConfigurationManager::romDirectoryPath(), QStringLiteral("ROM images (*.rom *.bin);;All files (*)"));
        if (!path.isEmpty()) m_impl->rom->setText(path);
    });
    connect(floppyBrowse, &QPushButton::clicked, this, [this]() {
        const auto path = chooseImage(this, QStringLiteral("Insert Floppy Image"), QStringLiteral("Floppy images (*.dsk *.img *.image *.dc42);;All files (*)"));
        if (!path.isEmpty()) m_impl->floppy->setText(path);
    });
    connect(floppyNew, &QPushButton::clicked, this, [this]() {
        const auto path = QFileDialog::getSaveFileName(this, QStringLiteral("Create Blank Floppy Image"), config::ConfigurationManager::diskImageDirectoryPath(), QStringLiteral("Raw 800K floppy (*.dsk)"));
        if (path.isEmpty()) return;
        QFile file(path);
        if (file.open(QIODevice::WriteOnly | QIODevice::Truncate) && file.resize(800 * 1024)) m_impl->floppy->setText(path);
        else QMessageBox::critical(this, windowTitle(), QStringLiteral("Could not create the floppy image."));
    });
    connect(addDisk, &QPushButton::clicked, this, [this, addScsiRow]() {
        const auto path = chooseImage(this, QStringLiteral("Select SCSI Media"), QStringLiteral("Disk and CD images (*.hda *.img *.iso);;All files (*)"));
        if (!path.isEmpty()) addScsiRow({ m_impl->scsi->rowCount() % 7, config::ScsiDeviceType::HardDisk, path, false });
    });
    connect(remove, &QPushButton::clicked, this, [this]() {
        if (m_impl->scsi->currentRow() >= 0) m_impl->scsi->removeRow(m_impl->scsi->currentRow());
    });
    connect(create, &QPushButton::clicked, this, [this, addScsiRow]() {
        DiskImageDialog dialog(this);
        if (dialog.exec() == QDialog::Accepted) addScsiRow({ m_impl->scsi->rowCount() % 7, config::ScsiDeviceType::HardDisk, dialog.imagePath(), false });
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
    result.ramSizeMiB = m_impl->ram->value();
    result.runtimeSpeed = static_cast<config::RuntimeSpeed>(m_impl->speed->currentData().toInt());
    result.cyclesPerFrame = m_impl->cycles->value();
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
    result.diskPath = result.scsiDevices.isEmpty() ? QString() : result.scsiDevices.first().imagePath;
    return result;
}

} // namespace cutemac::ui
