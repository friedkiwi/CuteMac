#include "cutemac/ui/RomManagerDialog.h"

#include <QApplication>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileInfo>
#include <QHeaderView>
#include <QPushButton>
#include <QTableWidget>
#include <QUrl>
#include <QVBoxLayout>

#include "cutemac/rom/RomCatalog.h"

namespace cutemac::ui {

RomManagerDialog::RomManagerDialog(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(QStringLiteral("ROM Manager"));
    resize(900, 430);
    auto* layout = new QVBoxLayout(this);
    m_table = new QTableWidget(0, 4);
    m_table->setHorizontalHeaderLabels({ QStringLiteral("Found"), QStringLiteral("ROM"), QStringLiteral("Revision"), QStringLiteral("File") });
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->verticalHeader()->hide();
    m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Stretch);
    layout->addWidget(m_table, 1);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close);
    auto* openFolder = buttons->addButton(QStringLiteral("Open Folder"), QDialogButtonBox::ActionRole);
    auto* refreshButton = buttons->addButton(QStringLiteral("Refresh"), QDialogButtonBox::ActionRole);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(openFolder, &QPushButton::clicked, this, []() {
        const auto path = rom::RomCatalog::shared().directoryPath();
        QDir().mkpath(path);
        QDesktopServices::openUrl(QUrl::fromLocalFile(path));
    });
    refreshButton->setToolTip(QStringLiteral("Rehash the ROM folder to pick up files added or removed outside CuteMac."));
    connect(refreshButton, &QPushButton::clicked, this, &RomManagerDialog::rescan);
    populate();
}

void RomManagerDialog::rescan()
{
    // Opening this dialog shows what CuteMac already knows; rehashing the
    // folder is what this button is for, and it is the only thing that asks.
    QApplication::setOverrideCursor(Qt::WaitCursor);
    rom::RomCatalog::shared().refresh();
    QApplication::restoreOverrideCursor();
    populate();
}

void RomManagerDialog::populate()
{
    const auto statuses = rom::RomCatalog::shared().statuses();
    m_table->setRowCount(statuses.size());
    for (int row = 0; row < statuses.size(); ++row) {
        const auto& status = statuses[row];
        auto* found = new QTableWidgetItem(status.found ? QStringLiteral("✓") : QStringLiteral("—"));
        found->setTextAlignment(Qt::AlignCenter);
        m_table->setItem(row, 0, found);
        m_table->setItem(row, 1, new QTableWidgetItem(status.definition.displayName));
        m_table->setItem(row, 2, new QTableWidgetItem(status.definition.revision));
        m_table->setItem(row, 3, new QTableWidgetItem(status.definition.kind == rom::RomKind::Embedded
                ? QStringLiteral("(embedded)") : (status.found ? QFileInfo(status.path).fileName() : QStringLiteral("(missing)"))));
        if (status.definition.discouraged) m_table->item(row, 2)->setToolTip(QStringLiteral("Older revision with known bugs; v3 is recommended."));
    }
}

} // namespace cutemac::ui
