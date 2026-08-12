#include "cutemac/session/HostRelativeMouseCapture.h"

#include <cmath>
#include <utility>

#include <QWidget>

#import <Cocoa/Cocoa.h>
#import <CoreGraphics/CoreGraphics.h>

namespace cutemac::session {

// Callbacks::lost is never raised here: dissociating the cursor is not a grab
// the window server can revoke, so the only way this capture ends early is the
// display widget losing focus, which the widget already handles itself.
class MacRelativeMouseCapture final : public HostRelativeMouseCapture {
public:
    ~MacRelativeMouseCapture() override { stop(); }

    [[nodiscard]] bool start(QWidget& target, Callbacks callbacks) override
    {
        stop();
        if (!target.isVisible()) return false;

        const auto nativeView = reinterpret_cast<NSView*>(target.winId());
        if (nativeView == nil) return false;
        NSWindow* window = [nativeView window];
        if (window == nil) return false;

        if (CGAssociateMouseAndMouseCursorPosition(false) != kCGErrorSuccess) {
            return false;
        }

        m_callbacks = std::move(callbacks);
        m_window = window;
        m_active = true;
        m_cursorHidden = true;
        m_previousAcceptsMouseMovedEvents = [m_window acceptsMouseMovedEvents];
        m_pendingDx = 0.0;
        m_pendingDy = 0.0;
        [NSCursor hide];
        [m_window setAcceptsMouseMovedEvents:YES];

        __block MacRelativeMouseCapture* self = this;
        m_monitor = [NSEvent addLocalMonitorForEventsMatchingMask:
                                NSEventMaskMouseMoved | NSEventMaskLeftMouseDragged
                                    | NSEventMaskRightMouseDragged | NSEventMaskOtherMouseDragged
                            handler:^NSEvent*(NSEvent* event) {
                                if (self == nullptr || !self->m_active || [event window] != self->m_window) {
                                    return event;
                                }

                                self->m_pendingDx += [event deltaX];
                                self->m_pendingDy += [event deltaY];
                                const auto dx = static_cast<int>(std::trunc(self->m_pendingDx));
                                const auto dy = static_cast<int>(std::trunc(self->m_pendingDy));
                                self->m_pendingDx -= dx;
                                self->m_pendingDy -= dy;
                                if ((dx != 0 || dy != 0) && self->m_callbacks.delta) {
                                    self->m_callbacks.delta(dx, dy);
                                }
                                return nil;
                            }];

        if (m_monitor == nil) {
            stop();
            return false;
        }
        return true;
    }

    void stop() override
    {
        if (m_monitor != nil) {
            [NSEvent removeMonitor:m_monitor];
            m_monitor = nil;
        }
        if (m_cursorHidden) {
            [NSCursor unhide];
            m_cursorHidden = false;
        }
        if (m_active) {
            CGAssociateMouseAndMouseCursorPosition(true);
            if (m_window != nil) [m_window setAcceptsMouseMovedEvents:m_previousAcceptsMouseMovedEvents];
        }
        m_active = false;
        m_window = nil;
        m_callbacks = {};
        m_pendingDx = 0.0;
        m_pendingDy = 0.0;
    }

    [[nodiscard]] bool active() const override { return m_active; }

private:
    Callbacks m_callbacks;
    NSWindow* m_window = nil;
    id m_monitor = nil;
    double m_pendingDx = 0.0;
    double m_pendingDy = 0.0;
    bool m_active = false;
    bool m_cursorHidden = false;
    bool m_previousAcceptsMouseMovedEvents = false;
};

std::unique_ptr<HostRelativeMouseCapture> createMacRelativeMouseCapture()
{
    return std::make_unique<MacRelativeMouseCapture>();
}

} // namespace cutemac::session
