#include "cutemac/session/HostRelativeMouseCapture.h"

#include <QtGlobal>

namespace cutemac::session {

HostRelativeMouseCapture::~HostRelativeMouseCapture() = default;

#if defined(Q_OS_MACOS)
std::unique_ptr<HostRelativeMouseCapture> createMacRelativeMouseCapture();
#elif defined(Q_OS_WIN)
std::unique_ptr<HostRelativeMouseCapture> createWindowsRelativeMouseCapture();
#endif

std::unique_ptr<HostRelativeMouseCapture> createHostRelativeMouseCapture()
{
#if defined(Q_OS_MACOS)
    return createMacRelativeMouseCapture();
#elif defined(Q_OS_WIN)
    return createWindowsRelativeMouseCapture();
#else
    return nullptr;
#endif
}

} // namespace cutemac::session
