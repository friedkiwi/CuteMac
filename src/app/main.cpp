#include <QApplication>
#include <QLabel>
#include <QMainWindow>
#include <QStringList>
#include <QVBoxLayout>
#include <QWidget>

#include "cutemac/machines/MachineCatalog.h"

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);

    auto* centralWidget = new QWidget;
    auto* layout = new QVBoxLayout(centralWidget);

    auto* title = new QLabel(QStringLiteral("CuteMac"));
    QFont titleFont = title->font();
    titleFont.setPointSize(titleFont.pointSize() + 8);
    titleFont.setBold(true);
    title->setFont(titleFont);

    QStringList machineNames;
    for (const auto& machine : cutemac::machines::MachineCatalog::supportedMachines()) {
        machineNames.append(QStringLiteral("%1 (%2)").arg(machine.displayName, machine.id));
    }

    auto* description = new QLabel(QStringLiteral("Qt 6 scaffold for modular classic Macintosh emulation."));
    auto* machines = new QLabel(machineNames.join(QStringLiteral("\n")));
    machines->setTextInteractionFlags(Qt::TextSelectableByMouse);

    layout->addWidget(title);
    layout->addWidget(description);
    layout->addWidget(machines);
    layout->addStretch();

    QMainWindow window;
    window.setWindowTitle(QStringLiteral("CuteMac"));
    window.setCentralWidget(centralWidget);
    window.resize(720, 420);
    window.show();

    return app.exec();
}
