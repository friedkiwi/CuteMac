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

    ok &= expect(cutemac::session::HostInputMapper::supportsRelativeCapture(QStringLiteral("cocoa")),
        "a non-Wayland platform must support relative mouse capture");
    ok &= expect(cutemac::session::HostInputMapper::supportsRelativeCapture(QStringLiteral("windows")),
        "a non-Wayland platform must support relative mouse capture");
    ok &= expect(!cutemac::session::HostInputMapper::supportsRelativeCapture(QStringLiteral("wayland")),
        "Wayland must fall back to the no-warp absolute-pointer path");
    ok &= expect(!cutemac::session::HostInputMapper::supportsRelativeCapture(QStringLiteral("wayland-egl")),
        "Wayland platform name variants must also fall back to the absolute-pointer path");
    ok &= expect(!cutemac::session::HostInputMapper::releaseChordLabel().isEmpty(),
        "the release chord label shown in the Display menu must not be empty");

    {
        // Qt swaps Ctrl/Cmd's symbolic modifiers on macOS by default, so the
        // physical Control key this chord requires is Qt::MetaModifier there
        // and Qt::ControlModifier everywhere else; see HostInputMapper::isReleaseChord.
        // A benign non-modifier key: constructing a QKeyEvent whose own key
        // value symbolically *is* one of its modifiers (e.g. Qt::Key_Control)
        // strips that modifier bit back out on macOS, which would defeat this
        // check regardless of platform semantics and isn't what the real
        // release-chord check (fired from keyPressEvent for any key) needs.
#if defined(Q_OS_MACOS)
        const auto releaseModifier = Qt::MetaModifier;
#else
        const auto releaseModifier = Qt::ControlModifier;
#endif
        const QKeyEvent chordHeld(QEvent::KeyPress, Qt::Key_A, releaseModifier | Qt::AltModifier);
        ok &= expect(cutemac::session::HostInputMapper::isReleaseChord(chordHeld),
            "the platform-correct Control+Option/Ctrl+Alt chord must be recognized as the release chord");
        const QKeyEvent halfChord(QEvent::KeyPress, Qt::Key_A, releaseModifier);
        ok &= expect(!cutemac::session::HostInputMapper::isReleaseChord(halfChord),
            "the modifier alone, without Option/Alt, must not be mistaken for the release chord");
    }

    return ok ? 0 : 1;
}
