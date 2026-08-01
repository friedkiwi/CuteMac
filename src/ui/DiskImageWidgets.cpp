#include "cutemac/ui/DiskImageWidgets.h"

#include <QComboBox>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QDoubleValidator>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QTableWidget>
#include <QUrl>
#include <QVBoxLayout>

namespace cutemac::ui {
namespace {

QString formatSize(qint64 bytes)
{
    if (bytes >= 1024LL * 1024 * 1024) return QStringLiteral("%1 GiB").arg(bytes / (1024.0 * 1024 * 1024), 0, 'f', 2);
    if (bytes >= 1024LL * 1024) return QStringLiteral("%1 MiB").arg(bytes / (1024.0 * 1024), 0, 'f', 1);
    return QStringLiteral("%1 KiB").arg(bytes / 1024.0, 0, 'f', 0);
}

QString imageFilter(storage::DiskImageType type)
{
    switch (type) {
    case storage::DiskImageType::Floppy: return QStringLiteral("Floppy images (*.dsk *.img *.image *.dc42);;All files (*)");
    case storage::DiskImageType::CdRom: return QStringLiteral("CD images (*.iso *.cdr);;All files (*)");
    case storage::DiskImageType::HardDisk: return QStringLiteral("Hard disk images (*.hda *.img *.dsk);;All files (*)");
    }
    return QStringLiteral("All files (*)");
}

bool importWithDialog(storage::DiskImageManager& manager, storage::DiskImageType type, QWidget* parent)
{
    const auto sources = QFileDialog::getOpenFileNames(parent, QStringLiteral("Import %1 Images").arg(storage::DiskImageManager::typeName(type)),
        QDir::homePath(), imageFilter(type));
    if (sources.isEmpty()) return false;
    QStringList importedPaths;
    const bool success = manager.importImages(sources, type, &importedPaths);
    if (!success) {
        QMessageBox::warning(parent, QStringLiteral("Import Disk Images"),
            QStringLiteral("%1 of %2 images were copied into the disk image library.").arg(importedPaths.size()).arg(sources.size()));
    }
    return !importedPaths.isEmpty();
}

class CreateImageDialog final : public QDialog {
public:
    CreateImageDialog(storage::DiskImageType type, QWidget* parent)
        : QDialog(parent), m_type(type)
    {
        setWindowTitle(type == storage::DiskImageType::Floppy ? QStringLiteral("Create Blank Floppy Image") : QStringLiteral("Create Blank Hard Disk Image"));
        auto* layout = new QVBoxLayout(this);
        auto* form = new QFormLayout;
        m_path = new QLineEdit;
        auto* browse = new QPushButton(QStringLiteral("Browse..."));
        auto* pathRow = new QHBoxLayout;
        pathRow->addWidget(m_path, 1);
        pathRow->addWidget(browse);
        form->addRow(QStringLiteral("Image file"), pathRow);
        m_preset = new QComboBox;
        if (type == storage::DiskImageType::Floppy) {
            m_preset->addItem(QStringLiteral("800K (double-sided Macintosh)"), 800LL * 1024);
            m_preset->addItem(QStringLiteral("1.4 MB (high-density Macintosh)"), 1440LL * 1024);
        } else {
            for (const auto size : { 20, 40, 80, 160, 230, 500, 1024 })
                m_preset->addItem(size == 1024 ? QStringLiteral("1 GB") : QStringLiteral("%1 MB").arg(size), static_cast<qint64>(size) * 1024 * 1024);
            m_preset->addItem(QStringLiteral("Custom"), -1);
        }
        form->addRow(QStringLiteral("Size"), m_preset);
        m_custom = new QLineEdit(QStringLiteral("100"));
        m_custom->setValidator(new QDoubleValidator(0.001, 999999.0, 3, m_custom));
        m_unit = new QComboBox;
        m_unit->addItem(QStringLiteral("MB"), 1024LL * 1024);
        m_unit->addItem(QStringLiteral("GB"), 1024LL * 1024 * 1024);
        auto* customRow = new QHBoxLayout;
        customRow->addWidget(m_custom, 1);
        customRow->addWidget(m_unit);
        form->addRow(QStringLiteral("Custom size"), customRow);
        layout->addLayout(form);
        auto* note = new QLabel(type == storage::DiskImageType::Floppy
                ? QStringLiteral("The image is created as a blank raw floppy and is not formatted.")
                : QStringLiteral("The image is created as a raw sparse file and is not partitioned or formatted."));
        note->setWordWrap(true);
        layout->addWidget(note);
        auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
        layout->addWidget(buttons);
        const auto updateCustom = [this]() {
            const bool enabled = m_type == storage::DiskImageType::HardDisk && m_preset->currentData().toLongLong() < 0;
            m_custom->setEnabled(enabled);
            m_unit->setEnabled(enabled);
        };
        connect(m_preset, &QComboBox::currentIndexChanged, this, updateCustom);
        connect(browse, &QPushButton::clicked, this, [this]() {
            const auto filter = m_type == storage::DiskImageType::Floppy ? QStringLiteral("Raw floppy (*.dsk)") : QStringLiteral("Raw hard disk (*.hda *.img)");
            const auto path = QFileDialog::getSaveFileName(this, windowTitle(), m_manager.libraryPath(), filter);
            if (!path.isEmpty()) m_path->setText(path);
        });
        connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
        connect(buttons, &QDialogButtonBox::accepted, this, [this]() {
            const auto path = m_path->text().trimmed();
            if (path.isEmpty()) { QMessageBox::warning(this, windowTitle(), QStringLiteral("Select an image file.")); return; }
            const qint64 preset = m_preset->currentData().toLongLong();
            const qint64 bytes = preset >= 0 ? preset : qRound64(m_custom->text().toDouble() * m_unit->currentData().toLongLong());
            if (bytes > 4LL * 1024 * 1024 * 1024
                && QMessageBox::warning(this, windowTitle(), QStringLiteral("Images over 4 GB may not work with vintage Macintosh software. Create it anyway?"),
                       QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes) return;
            if (!m_manager.createImage(path, m_type, bytes)) { QMessageBox::critical(this, windowTitle(), QStringLiteral("Could not create the disk image.")); return; }
            m_createdPath = QFileInfo(path).absoluteFilePath();
            accept();
        });
        updateCustom();
    }
    QString createdPath() const { return m_createdPath; }
private:
    storage::DiskImageType m_type;
    storage::DiskImageManager m_manager;
    QLineEdit* m_path = nullptr;
    QComboBox* m_preset = nullptr;
    QLineEdit* m_custom = nullptr;
    QComboBox* m_unit = nullptr;
    QString m_createdPath;
};

} // namespace

class DiskImageManagerDialog::Impl {
public:
    storage::DiskImageManager manager;
    QTableWidget* table = nullptr;
    QComboBox* importType = nullptr;
};

DiskImageManagerDialog::DiskImageManagerDialog(QWidget* parent) : QDialog(parent), m_impl(std::make_unique<Impl>())
{
    setWindowTitle(QStringLiteral("Disk Image Manager"));
    resize(760, 440);
    auto* layout = new QVBoxLayout(this);
    m_impl->table = new QTableWidget(0, 4);
    m_impl->table->setHorizontalHeaderLabels({ QStringLiteral("Name"), QStringLiteral("Type"), QStringLiteral("Size"), QStringLiteral("Location") });
    m_impl->table->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Stretch);
    m_impl->table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_impl->table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_impl->table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    layout->addWidget(m_impl->table, 1);
    auto* controls = new QHBoxLayout;
    m_impl->importType = new QComboBox;
    for (const auto type : { storage::DiskImageType::Floppy, storage::DiskImageType::CdRom, storage::DiskImageType::HardDisk })
        m_impl->importType->addItem(storage::DiskImageManager::typeName(type), static_cast<int>(type));
    auto* import = new QPushButton(QStringLiteral("Import..."));
    auto* exportImage = new QPushButton(QStringLiteral("Export..."));
    auto* createFloppy = new QPushButton(QStringLiteral("New Floppy..."));
    auto* createHardDisk = new QPushButton(QStringLiteral("New Hard Disk..."));
    auto* openFolder = new QPushButton(QStringLiteral("Open Folder"));
    controls->addWidget(m_impl->importType);
    for (auto* button : { import, exportImage, createFloppy, createHardDisk }) controls->addWidget(button);
    controls->addStretch();
    controls->addWidget(openFolder);
    layout->addLayout(controls);
    auto* close = new QDialogButtonBox(QDialogButtonBox::Close);
    layout->addWidget(close);
    const auto reload = [this]() {
        (void)m_impl->manager.refresh();
        const auto images = m_impl->manager.images();
        m_impl->table->setRowCount(images.size());
        for (int row = 0; row < images.size(); ++row) {
            auto* name = new QTableWidgetItem(QFileInfo(images[row].path).fileName());
            name->setData(Qt::UserRole, images[row].path);
            m_impl->table->setItem(row, 0, name);
            m_impl->table->setItem(row, 1, new QTableWidgetItem(storage::DiskImageManager::typeName(images[row].type)));
            m_impl->table->setItem(row, 2, new QTableWidgetItem(formatSize(images[row].sizeBytes)));
            m_impl->table->setItem(row, 3, new QTableWidgetItem(images[row].path));
        }
        if (!images.isEmpty()) m_impl->table->selectRow(0);
    };
    connect(import, &QPushButton::clicked, this, [this, reload]() {
        const auto type = static_cast<storage::DiskImageType>(m_impl->importType->currentData().toInt());
        if (importWithDialog(m_impl->manager, type, this)) reload();
    });
    connect(exportImage, &QPushButton::clicked, this, [this]() {
        const int row = m_impl->table->currentRow();
        if (row < 0) return;
        const auto source = m_impl->table->item(row, 0)->data(Qt::UserRole).toString();
        const auto destination = QFileDialog::getSaveFileName(this, QStringLiteral("Export Disk Image"), QFileInfo(source).fileName());
        if (!destination.isEmpty() && !m_impl->manager.exportImage(source, destination)) QMessageBox::critical(this, windowTitle(), QStringLiteral("Could not export the disk image."));
    });
    connect(createFloppy, &QPushButton::clicked, this, [this, reload]() { if (!createDiskImage(storage::DiskImageType::Floppy, this).isEmpty()) reload(); });
    connect(createHardDisk, &QPushButton::clicked, this, [this, reload]() { if (!createDiskImage(storage::DiskImageType::HardDisk, this).isEmpty()) reload(); });
    connect(openFolder, &QPushButton::clicked, this, [this]() { QDesktopServices::openUrl(QUrl::fromLocalFile(m_impl->manager.libraryPath())); });
    connect(close, &QDialogButtonBox::rejected, this, &QDialog::reject);
    reload();
}

DiskImageManagerDialog::~DiskImageManagerDialog() = default;

class DiskImagePickerDialog::Impl {
public:
    storage::DiskImageType type;
    storage::DiskImageManager manager;
    QTableWidget* table = nullptr;
};

DiskImagePickerDialog::DiskImagePickerDialog(storage::DiskImageType type, const QString& title, QWidget* parent)
    : QDialog(parent), m_impl(std::make_unique<Impl>())
{
    m_impl->type = type;
    setWindowTitle(title);
    resize(620, 360);
    auto* layout = new QVBoxLayout(this);
    m_impl->table = new QTableWidget(0, 3);
    m_impl->table->setHorizontalHeaderLabels({ QStringLiteral("Name"), QStringLiteral("Size"), QStringLiteral("Location") });
    m_impl->table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    m_impl->table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_impl->table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_impl->table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    layout->addWidget(m_impl->table, 1);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Open | QDialogButtonBox::Cancel);
    auto* import = buttons->addButton(QStringLiteral("Import..."), QDialogButtonBox::ActionRole);
    layout->addWidget(buttons);
    const auto reload = [this]() {
        (void)m_impl->manager.refresh();
        const auto images = m_impl->manager.images(m_impl->type);
        m_impl->table->setRowCount(images.size());
        for (int row = 0; row < images.size(); ++row) {
            auto* name = new QTableWidgetItem(QFileInfo(images[row].path).fileName());
            name->setData(Qt::UserRole, images[row].path);
            m_impl->table->setItem(row, 0, name);
            m_impl->table->setItem(row, 1, new QTableWidgetItem(formatSize(images[row].sizeBytes)));
            m_impl->table->setItem(row, 2, new QTableWidgetItem(images[row].path));
        }
        if (!images.isEmpty()) m_impl->table->selectRow(0);
    };
    connect(import, &QPushButton::clicked, this, [this, reload]() { if (importWithDialog(m_impl->manager, m_impl->type, this)) reload(); });
    connect(buttons, &QDialogButtonBox::accepted, this, [this]() { if (m_impl->table->currentRow() >= 0) accept(); });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(m_impl->table, &QTableWidget::cellDoubleClicked, this, [this](int, int) { accept(); });
    reload();
}

DiskImagePickerDialog::~DiskImagePickerDialog() = default;

QString DiskImagePickerDialog::selectedImagePath() const
{
    const int row = m_impl->table->currentRow();
    return row < 0 ? QString() : m_impl->table->item(row, 0)->data(Qt::UserRole).toString();
}

QString DiskImagePickerDialog::getImage(storage::DiskImageType type, const QString& title, QWidget* parent)
{
    DiskImagePickerDialog dialog(type, title, parent);
    return dialog.exec() == QDialog::Accepted ? dialog.selectedImagePath() : QString();
}

QString createDiskImage(storage::DiskImageType type, QWidget* parent)
{
    if (type == storage::DiskImageType::CdRom) return {};
    CreateImageDialog dialog(type, parent);
    return dialog.exec() == QDialog::Accepted ? dialog.createdPath() : QString();
}

} // namespace cutemac::ui
