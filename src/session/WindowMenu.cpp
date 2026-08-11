#include "cutemac/session/WindowMenu.h"

#include <QMainWindow>

#if defined(Q_OS_MACOS)
#include <algorithm>

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QKeySequence>
#include <QList>
#include <QMenu>
#include <QMenuBar>
#include <QWidget>
#endif

namespace cutemac::session {

#if defined(Q_OS_MACOS)

namespace {

// Every visible top-level window, ordered by title so the menu does not
// reshuffle between openings. Dialogs are deliberately excluded: only the
// session and profile-manager windows are QMainWindow.
QList<QMainWindow*> orderedWindows()
{
    QList<QMainWindow*> windows;
    for (auto* widget : QApplication::topLevelWidgets()) {
        auto* window = qobject_cast<QMainWindow*>(widget);
        if (window == nullptr || !window->isVisible()) continue;
        windows.append(window);
    }
    std::sort(windows.begin(), windows.end(), [](const QMainWindow* left, const QMainWindow* right) {
        const auto order = QString::compare(left->windowTitle(), right->windowTitle(), Qt::CaseInsensitive);
        // Titles can collide while a session is still unsaved; fall back to
        // pointer order so the comparator stays a strict weak ordering.
        return order != 0 ? order < 0 : left < right;
    });
    return windows;
}

void raiseWindow(QMainWindow* window)
{
    if (window->isMinimized()) window->showNormal();
    window->raise();
    window->activateWindow();
}

void rebuild(QMenu* menu, QMainWindow* owner)
{
    menu->clear();

    auto* minimize = menu->addAction(QStringLiteral("Minimize"));
    minimize->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_M));
    QObject::connect(minimize, &QAction::triggered, owner, [owner]() { owner->showMinimized(); });

    auto* zoom = menu->addAction(QStringLiteral("Zoom"));
    QObject::connect(zoom, &QAction::triggered, owner, [owner]() {
        if (owner->isMaximized()) owner->showNormal();
        else owner->showMaximized();
    });

    menu->addSeparator();

    auto* bringAllToFront = menu->addAction(QStringLiteral("Bring All to Front"));
    QObject::connect(bringAllToFront, &QAction::triggered, owner, [owner]() {
        for (auto* window : orderedWindows()) {
            if (window != owner) raiseWindow(window);
        }
        // The window whose menu was used ends up frontmost.
        raiseWindow(owner);
    });

    const auto windows = orderedWindows();
    if (windows.isEmpty()) return;

    menu->addSeparator();
    // Exclusive so the checkmark reads as "this is the front window" rather
    // than as an independent toggle per entry.
    auto* group = new QActionGroup(menu);
    group->setExclusive(true);
    for (auto* window : windows) {
        auto title = window->windowTitle();
        if (title.isEmpty()) title = QStringLiteral("Untitled");
        auto* action = menu->addAction(title);
        action->setCheckable(true);
        action->setChecked(window == owner);
        action->setActionGroup(group);
        QObject::connect(action, &QAction::triggered, window, [window]() { raiseWindow(window); });
    }
}

} // namespace

void WindowMenu::install(QMainWindow* window)
{
    if (window == nullptr) return;
    auto* menu = window->menuBar()->addMenu(QStringLiteral("Window"));
    // Rebuilding on open keeps titles and the session list current; Qt forwards
    // this to the native menu through the Cocoa menu delegate.
    QObject::connect(menu, &QMenu::aboutToShow, menu, [menu, window]() { rebuild(menu, window); });
    rebuild(menu, window);
}

#else

void WindowMenu::install(QMainWindow*) { }

#endif

} // namespace cutemac::session
