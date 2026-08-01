#include "cutemac/session/HostInputMapper.h"

#include <QKeyEvent>

namespace cutemac::session {

int HostInputMapper::macKeyCode(const QKeyEvent& event)
{
    switch (event.key()) {
    case Qt::Key_A: return 0x00; case Qt::Key_S: return 0x01; case Qt::Key_D: return 0x02; case Qt::Key_F: return 0x03;
    case Qt::Key_H: return 0x04; case Qt::Key_G: return 0x05; case Qt::Key_Z: return 0x06; case Qt::Key_X: return 0x07;
    case Qt::Key_C: return 0x08; case Qt::Key_V: return 0x09; case Qt::Key_B: return 0x0b; case Qt::Key_Q: return 0x0c;
    case Qt::Key_W: return 0x0d; case Qt::Key_E: return 0x0e; case Qt::Key_R: return 0x0f; case Qt::Key_Y: return 0x10;
    case Qt::Key_T: return 0x11; case Qt::Key_1: return 0x12; case Qt::Key_2: return 0x13; case Qt::Key_3: return 0x14;
    case Qt::Key_4: return 0x15; case Qt::Key_6: return 0x16; case Qt::Key_5: return 0x17; case Qt::Key_Equal: return 0x18;
    case Qt::Key_9: return 0x19; case Qt::Key_7: return 0x1a; case Qt::Key_Minus: return 0x1b; case Qt::Key_8: return 0x1c;
    case Qt::Key_0: return 0x1d; case Qt::Key_BracketRight: return 0x1e; case Qt::Key_O: return 0x1f; case Qt::Key_U: return 0x20;
    case Qt::Key_BracketLeft: return 0x21; case Qt::Key_I: return 0x22; case Qt::Key_P: return 0x23; case Qt::Key_Return: return 0x24;
    case Qt::Key_L: return 0x25; case Qt::Key_J: return 0x26; case Qt::Key_Apostrophe: return 0x27; case Qt::Key_K: return 0x28;
    case Qt::Key_Semicolon: return 0x29; case Qt::Key_Backslash: return 0x2a; case Qt::Key_Comma: return 0x2b; case Qt::Key_Slash: return 0x2c;
    case Qt::Key_N: return 0x2d; case Qt::Key_M: return 0x2e; case Qt::Key_Period: return 0x2f; case Qt::Key_Tab: return 0x30;
    case Qt::Key_Space: return 0x31; case Qt::Key_QuoteLeft: return 0x32; case Qt::Key_Backspace: return 0x33; case Qt::Key_Escape: return 0x35;
    case Qt::Key_Control: return 0x36; case Qt::Key_Meta: return 0x37; case Qt::Key_Shift: return 0x38; case Qt::Key_CapsLock: return 0x39;
    case Qt::Key_Alt: return 0x3a; case Qt::Key_Left: return 0x3b; case Qt::Key_Right: return 0x3c; case Qt::Key_Down: return 0x3d;
    case Qt::Key_Up: return 0x3e;
    default: return -1;
    }
}

} // namespace cutemac::session
