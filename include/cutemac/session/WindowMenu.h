#pragma once

class QMainWindow;

namespace cutemac::session {

// macOS expects every window to carry a Window menu with Minimize, Zoom, and
// the list of open windows. Qt creates the application menu on macOS but not
// this one, and CuteMac runs several session windows at once, so both window
// types install it.
//
// The menu is rebuilt whenever it is about to open, so window titles and the
// session list are always current without observers in each window.
class WindowMenu {
public:
    // A no-op on platforms whose conventions have no Window menu.
    static void install(QMainWindow* window);
};

} // namespace cutemac::session
