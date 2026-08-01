#include "cutemac/ui/ConfigurationDialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

namespace cutemac::ui {

ConfigurationDialog::ConfigurationDialog(config::Configuration configuration, QWidget* parent)
    : QDialog(parent)
    , m_configuration(std::move(configuration))
{
    setWindowTitle(QStringLiteral("Machine Configuration"));

    auto* layout = new QVBoxLayout(this);
    auto* form = new QFormLayout;
    m_name = new QLineEdit(m_configuration.profileName);
    m_machine = new QComboBox;
    m_machine->addItem(QStringLiteral("Macintosh Plus"), QStringLiteral("mac-plus"));

    const auto makePathRow = [this, form](const QString& label, QLineEdit*& field, const QString& path,
                                 const QString& title, const QString& directory, const QString& filter) {
        field = new QLineEdit(path);
        auto* browse = new QPushButton(QStringLiteral("Browse..."));
        auto* row = new QHBoxLayout;
        row->addWidget(field, 1);
        row->addWidget(browse);
        form->addRow(label, row);
        connect(browse, &QPushButton::clicked, this, [this, field, title, directory, filter]() {
            const auto selected = QFileDialog::getOpenFileName(this, title, directory, filter);
            if (!selected.isEmpty()) field->setText(selected);
        });
    };

    form->addRow(QStringLiteral("Name"), m_name);
    form->addRow(QStringLiteral("Machine"), m_machine);
    makePathRow(QStringLiteral("ROM"), m_romPath, m_configuration.romPath, QStringLiteral("Select Mac Plus ROM"),
        config::ConfigurationManager::romDirectoryPath(), QStringLiteral("ROM images (*.rom *.bin);;All files (*)"));
    makePathRow(QStringLiteral("Disk image"), m_diskPath, m_configuration.diskPath, QStringLiteral("Select disk image"),
        config::ConfigurationManager::diskImageDirectoryPath(), QStringLiteral("Disk images (*.dsk *.img *.image);;All files (*)"));
    makePathRow(QStringLiteral("Floppy image"), m_floppyPath, m_configuration.floppyPath, QStringLiteral("Select floppy image"),
        config::ConfigurationManager::diskImageDirectoryPath(), QStringLiteral("Floppy images (*.dsk *.img *.image *.dc42);;All files (*)"));

    m_ramSize = new QSpinBox;
    m_ramSize->setRange(1, 4);
    m_ramSize->setSuffix(QStringLiteral(" MiB"));
    m_ramSize->setValue(m_configuration.ramSizeMiB);
    form->addRow(QStringLiteral("RAM"), m_ramSize);

    m_cyclesPerFrame = new QSpinBox;
    m_cyclesPerFrame->setRange(1000, 2000000);
    m_cyclesPerFrame->setSingleStep(10000);
    m_cyclesPerFrame->setValue(m_configuration.cyclesPerFrame);
    form->addRow(QStringLiteral("Cycles/frame"), m_cyclesPerFrame);

    m_runtimeSpeed = new QComboBox;
    m_runtimeSpeed->addItem(QStringLiteral("Realtime"), static_cast<int>(config::RuntimeSpeed::Realtime));
    m_runtimeSpeed->addItem(QStringLiteral("Unlimited"), static_cast<int>(config::RuntimeSpeed::Unlimited));
    m_runtimeSpeed->setCurrentIndex(m_runtimeSpeed->findData(static_cast<int>(m_configuration.runtimeSpeed)));
    form->addRow(QStringLiteral("Runtime speed"), m_runtimeSpeed);

    m_skipRamPatternTest = new QCheckBox;
    m_skipRamPatternTest->setChecked(m_configuration.skipRamPatternTest);
    form->addRow(QStringLiteral("Skip RAM pattern test"), m_skipRamPatternTest);
    layout->addLayout(form);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, this, [this]() {
        if (m_name->text().trimmed().isEmpty()) {
            QMessageBox::warning(this, QStringLiteral("Machine Configuration"), QStringLiteral("Profile name is required."));
            return;
        }
        accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

config::Configuration ConfigurationDialog::configuration() const
{
    auto result = m_configuration;
    result.profileName = m_name->text().trimmed();
    result.machineId = m_machine->currentData().toString();
    result.romPath = m_romPath->text().trimmed();
    result.diskPath = m_diskPath->text().trimmed();
    result.floppyPath = m_floppyPath->text().trimmed();
    result.ramSizeMiB = m_ramSize->value();
    result.cyclesPerFrame = m_cyclesPerFrame->value();
    result.runtimeSpeed = static_cast<config::RuntimeSpeed>(m_runtimeSpeed->currentData().toInt());
    result.skipRamPatternTest = m_skipRamPatternTest->isChecked();
    return result;
}

} // namespace cutemac::ui
