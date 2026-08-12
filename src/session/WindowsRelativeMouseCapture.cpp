#include "cutemac/session/HostRelativeMouseCapture.h"

#include <cmath>
#include <cstring>
#include <memory>
#include <utility>
#include <vector>

#include <QByteArray>
#include <QCoreApplication>
#include <QAbstractNativeEventFilter>
#include <QMetaObject>
#include <QPoint>
#include <QWidget>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace cutemac::session {
namespace {

constexpr USHORT kGenericDesktopPage = 0x01;
constexpr USHORT kMouseUsage = 0x02;

// The normalized coordinate space absolute-reporting devices use, per the
// RAWMOUSE documentation.
constexpr int kAbsoluteRange = 65535;

class WindowsRelativeMouseCapture final : public HostRelativeMouseCapture, public QAbstractNativeEventFilter {
public:
    ~WindowsRelativeMouseCapture() override { stop(); }

    [[nodiscard]] bool start(QWidget& target, Callbacks callbacks) override
    {
        stop();
        if (!target.isVisible()) return false;
        auto* topLevel = target.window();
        if (topLevel == nullptr) return false;
        const auto hwnd = reinterpret_cast<HWND>(topLevel->winId());
        if (hwnd == nullptr) return false;

        RAWINPUTDEVICE device {};
        device.usUsagePage = kGenericDesktopPage;
        device.usUsage = kMouseUsage;
        // RIDEV_INPUTSINK names our own top-level window explicitly instead of
        // following native keyboard focus. Qt hands focus to child widgets
        // without promoting them to native windows, so a focus-following
        // registration is not reliably delivered to the display widget. The
        // handler drops anything that arrives while another application is
        // foreground, which is what INPUTSINK otherwise lets through.
        device.dwFlags = RIDEV_INPUTSINK;
        device.hwndTarget = hwnd;
        if (RegisterRawInputDevices(&device, 1, sizeof(device)) == FALSE) return false;

        m_target = &target;
        m_hwnd = hwnd;
        m_callbacks = std::move(callbacks);
        m_alive = std::make_shared<bool>(true);
        m_active = true;
        m_lostPending = false;
        m_absoluteValid = false;
        m_clip = RECT {};
        m_restoreCursorPosition = GetCursorPos(&m_cursorPositionAtStart) != FALSE;

        if (!applyCursorClip()) {
            stop();
            return false;
        }
        QCoreApplication::instance()->installNativeEventFilter(this);
        return true;
    }

    void stop() override
    {
        // Invalidated before anything else so a queued loss notification that
        // is already in the event loop does not run against a stopped capture.
        if (m_alive) {
            *m_alive = false;
            m_alive.reset();
        }
        if (m_active) {
            QCoreApplication::instance()->removeNativeEventFilter(this);

            RAWINPUTDEVICE device {};
            device.usUsagePage = kGenericDesktopPage;
            device.usUsage = kMouseUsage;
            device.dwFlags = RIDEV_REMOVE;
            // RIDEV_REMOVE requires a null target window.
            device.hwndTarget = nullptr;
            RegisterRawInputDevices(&device, 1, sizeof(device));

            ClipCursor(nullptr);
            // The guest moved its own pointer while captured; the host pointer
            // should come back where the user left it rather than wherever the
            // confinement rectangle happened to strand it.
            if (m_restoreCursorPosition) SetCursorPos(m_cursorPositionAtStart.x, m_cursorPositionAtStart.y);
        }
        m_active = false;
        m_lostPending = false;
        m_restoreCursorPosition = false;
        m_hwnd = nullptr;
        m_target = nullptr;
        m_callbacks = {};
        m_clip = RECT {};
        m_buffer.clear();
    }

    [[nodiscard]] bool active() const override { return m_active; }

    bool nativeEventFilter(const QByteArray& eventType, void* message, qintptr*) override
    {
        if (!m_active || eventType != QByteArrayLiteral("windows_generic_MSG")) return false;
        const auto* msg = static_cast<const MSG*>(message);
        switch (msg->message) {
        case WM_INPUT:
            handleRawInput(reinterpret_cast<HRAWINPUT>(msg->lParam));
            break;
        case WM_ACTIVATEAPP:
            if (msg->wParam == FALSE) notifyLost();
            break;
        default:
            break;
        }
        // Never consumed: WM_INPUT still has to reach DefWindowProc so the
        // system can clean the message up, and the rest are only observed.
        return false;
    }

private:
    void handleRawInput(HRAWINPUT handle)
    {
        if (GetForegroundWindow() != m_hwnd) {
            notifyLost();
            return;
        }

        UINT size = 0;
        if (GetRawInputData(handle, RID_INPUT, nullptr, &size, sizeof(RAWINPUTHEADER)) != 0 || size == 0) return;
        if (m_buffer.size() < size) m_buffer.resize(size);
        if (GetRawInputData(handle, RID_INPUT, m_buffer.data(), &size, sizeof(RAWINPUTHEADER)) != size) return;

        const auto* input = reinterpret_cast<const RAWINPUT*>(m_buffer.data());
        if (input->header.dwType != RIM_TYPEMOUSE) return;
        const auto& mouse = input->data.mouse;

        int dx = 0;
        int dy = 0;
        if ((mouse.usFlags & MOUSE_MOVE_ABSOLUTE) != 0) {
            // Remote desktop sessions, tablets and several hypervisors report a
            // normalized absolute position instead of relative counts.
            // Differencing successive reports is the only way to recover motion
            // from them, and skipping this would leave capture completely dead
            // rather than merely degraded on those hosts.
            const bool virtualDesktop = (mouse.usFlags & MOUSE_VIRTUAL_DESKTOP) != 0;
            const int originX = virtualDesktop ? GetSystemMetrics(SM_XVIRTUALSCREEN) : 0;
            const int originY = virtualDesktop ? GetSystemMetrics(SM_YVIRTUALSCREEN) : 0;
            const int width = GetSystemMetrics(virtualDesktop ? SM_CXVIRTUALSCREEN : SM_CXSCREEN);
            const int height = GetSystemMetrics(virtualDesktop ? SM_CYVIRTUALSCREEN : SM_CYSCREEN);
            const int x = originX + MulDiv(mouse.lLastX, width, kAbsoluteRange);
            const int y = originY + MulDiv(mouse.lLastY, height, kAbsoluteRange);
            if (m_absoluteValid) {
                dx = x - m_absoluteX;
                dy = y - m_absoluteY;
            }
            m_absoluteX = x;
            m_absoluteY = y;
            m_absoluteValid = true;
        } else {
            dx = mouse.lLastX;
            dy = mouse.lLastY;
        }

        // Recomputed per event so a moved, resized or rescaled window cannot
        // strand the confinement rectangle where the display used to be.
        // applyCursorClip() only calls into the system when the rectangle
        // actually changes, so the common case is a comparison.
        if (!applyCursorClip()) {
            notifyLost();
            return;
        }
        if ((dx != 0 || dy != 0) && m_callbacks.delta) m_callbacks.delta(dx, dy);
    }

    // Confining the pointer is what keeps a hidden cursor from wandering onto
    // another window and clicking it. Raw input reports motion regardless of
    // where the cursor sits, so unlike the warp fallback nothing here has to
    // recenter it -- it simply stops at the edges of the display.
    [[nodiscard]] bool applyCursorClip()
    {
        if (m_target == nullptr || m_hwnd == nullptr) return false;

        // ClipCursor works in physical pixels while Qt reports device
        // independent ones. Anchoring on the top-level window's client origin,
        // which the system reports in physical pixels, avoids having to
        // reconstruct a screen's physical origin from Qt's logical geometry.
        POINT clientOrigin { 0, 0 };
        if (ClientToScreen(m_hwnd, &clientOrigin) == FALSE) return false;

        const auto offset = m_target->mapTo(m_target->window(), QPoint(0, 0));
        const auto scale = m_target->devicePixelRatio();
        RECT clip {};
        clip.left = clientOrigin.x + static_cast<LONG>(std::lround(offset.x() * scale));
        clip.top = clientOrigin.y + static_cast<LONG>(std::lround(offset.y() * scale));
        clip.right = clip.left + static_cast<LONG>(std::lround(m_target->width() * scale));
        clip.bottom = clip.top + static_cast<LONG>(std::lround(m_target->height() * scale));
        if (clip.right <= clip.left || clip.bottom <= clip.top) return false;

        if (std::memcmp(&clip, &m_clip, sizeof(RECT)) == 0) return true;
        if (ClipCursor(&clip) == FALSE) return false;
        m_clip = clip;
        return true;
    }

    // Queued rather than called directly: the frontend responds by calling
    // stop(), which removes this native event filter, and the dispatcher is
    // still walking its filter list at this point.
    void notifyLost()
    {
        if (!m_active || m_lostPending || !m_callbacks.lost) return;
        m_lostPending = true;
        auto alive = m_alive;
        auto lost = m_callbacks.lost;
        QMetaObject::invokeMethod(
            QCoreApplication::instance(),
            [alive = std::move(alive), lost = std::move(lost)]() {
                if (alive && *alive) lost();
            },
            Qt::QueuedConnection);
    }

    Callbacks m_callbacks;
    std::shared_ptr<bool> m_alive;
    QWidget* m_target = nullptr;
    HWND m_hwnd = nullptr;
    std::vector<BYTE> m_buffer;
    RECT m_clip {};
    POINT m_cursorPositionAtStart {};
    int m_absoluteX = 0;
    int m_absoluteY = 0;
    bool m_absoluteValid = false;
    bool m_restoreCursorPosition = false;
    bool m_active = false;
    bool m_lostPending = false;
};

} // namespace

std::unique_ptr<HostRelativeMouseCapture> createWindowsRelativeMouseCapture()
{
    return std::make_unique<WindowsRelativeMouseCapture>();
}

} // namespace cutemac::session
