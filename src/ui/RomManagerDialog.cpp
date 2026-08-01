#include "cutemac/ui/RomManagerDialog.h"

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
    resize(760, 430);
    auto* layout = new QVBoxLayout(this);
    m_table = new QTableWidget(0, 5);
    m_table->setHorizontalHeaderLabels({ QStringLiteral("Found"), QStringLiteral("ROM"), QStringLiteral("Revision"), QStringLiteral("File"), QStringLiteral("Checksum") });
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->verticalHeader()->hide();
    m_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Stretch);
    layout->addWidget(m_table, 1);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close);
    auto* openFolder = buttons->addButton(QStringLiteral("Open Folder"), QDialogButtonBox::ActionRole);
    auto* refreshButton = buttons->addButton(QStringLiteral("Refresh"), QDialogButtonBox::ActionRole);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(openFolder, &QPushButton::clicked, this, []() {
        const auto path = rom::RomCatalog().directoryPath();
        QDir().mkpath(path);
        QDesktopServices::openUrl(QUrl::fromLocalFile(path));
    });
    connect(refreshButton, &QPushButton::clicked, this, &RomManagerDialog::refresh);
    refresh();
}

void RomManagerDialog::refresh()
{
    const rom::RomCatalog catalog;
    const auto statuses = catalog.statuses();
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
        const auto checksum = !status.definition.sha256.isEmpty() ? status.definition.sha256
            : (!status.definition.sha1.isEmpty() ? status.definition.sha1 : status.definition.headerChecksum);
        m_table->setItem(row, 4, new QTableWidgetItem(QString::fromLatin1(checksum.toHex())));
        if (status.definition.discouraged) m_table->item(row, 2)->setToolTip(QStringLiteral("Older revision with known bugs; v3 is recommended."));
    }
    m_table->resizeColumnToContents(0);
    m_table->resizeColumnToContents(2);
    m_table->resizeColumnToContents(4);
}

} // namespace cutemac::ui
