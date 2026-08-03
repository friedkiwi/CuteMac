#include <iostream>

#include <QEvent>
#include <QKeyEvent>

#include "cutemac/session/HostInputMapper.h"

namespace {

bool expect(bool condition, const char* message)
{
    if (!condition) std::cerr << "FAIL: " << message << '\n';
    return condition;
}

int mappedKey(int key, Qt::KeyboardModifiers modifiers = Qt::NoModifier)
{
    const QKeyEvent event(QEvent::KeyPress, key, modifiers);
    return cutemac::session::HostInputMapper::macKeyCode(event);
}

} // namespace

int main()
{
    bool ok = true;
    ok &= expect(mappedKey(Qt::Key_2) == 0x13, "main keyboard 2 mapping changed");
    ok &= expect(mappedKey(Qt::Key_2, Qt::ShiftModifier) == 0x13,
        "Shift must remain a separate guest key instead of changing the base key code");
    ok &= expect(mappedKey(Qt::Key_At, Qt::ShiftModifier) == 0x13,
        "a backend which reports the shifted @ symbol must map it to the Macintosh 2 key");
    ok &= expect(mappedKey(Qt::Key_Plus, Qt::ShiftModifier) == 0x18,
        "a shifted plus symbol must map to the main keyboard equals key");
    ok &= expect(mappedKey(Qt::Key_2, Qt::KeypadModifier) == 0x54, "keypad 2 mapping is wrong");
    ok &= expect(mappedKey(Qt::Key_Enter, Qt::KeypadModifier) == 0x4c, "keypad Enter mapping is wrong");
    ok &= expect(mappedKey(Qt::Key_Plus, Qt::KeypadModifier) == 0x45, "keypad plus mapping is wrong");
    ok &= expect(mappedKey(Qt::Key_Period, Qt::KeypadModifier) == 0x41, "keypad decimal mapping is wrong");
    return ok ? 0 : 1;
}
