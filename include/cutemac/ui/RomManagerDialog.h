#pragma once

#include <QDialog>

class QTableWidget;

namespace cutemac::ui {

class RomManagerDialog final : public QDialog {
public:
    explicit RomManagerDialog(QWidget* parent = nullptr);

private:
    // populate() fills the table from the cached catalog; rescan() is the
    // Refresh button, and the only path that rehashes the ROM folder.
    void populate();
    void rescan();
    QTableWidget* m_table = nullptr;
};

} // namespace cutemac::ui
