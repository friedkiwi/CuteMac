#include "cutemac/devices/network/SlirpEthernetBackend.h"

#include <QByteArray>
#include <QStringList>
#include <QtEndian>

#include <array>
#include <chrono>
#include <deque>
#include <utility>
#include <vector>

#if CUTEMAC_HAS_LIBSLIRP
#include <slirp/libslirp.h>
#ifndef _WIN32
#include <poll.h>
#endif
#endif

namespace cutemac::devices::network {

namespace {

std::uint32_t ipv4ToHostOrder(const std::array<std::uint8_t, 4>& address)
{
    return (static_cast<std::uint32_t>(address[0]) << 24)
        | (static_cast<std::uint32_t>(address[1]) << 16)
        | (static_cast<std::uint32_t>(address[2]) << 8)
        | static_cast<std::uint32_t>(address[3]);
}

} // namespace

std::array<std::uint8_t, 4> parseIpv4Address(
    const QString& text,
    const std::array<std::uint8_t, 4>& fallback)
{
    const auto parts = text.split(QLatin1Char('.'));
    if (parts.size() != 4) return fallback;
    std::array<std::uint8_t, 4> result {};
    for (int index = 0; index < 4; ++index) {
        bool ok = false;
        const auto value = parts[index].toUInt(&ok);
        if (!ok || value > 255) return fallback;
        result[static_cast<std::size_t>(index)] = static_cast<std::uint8_t>(value);
    }
    return result;
}

class SlirpEthernetBackend::Impl {
public:
    explicit Impl(SlirpEthernetConfiguration configuration)
        : m_configuration(std::move(configuration))
    {
        initialize();
    }

    ~Impl() { close(); }

    void poll()
    {
#if CUTEMAC_HAS_LIBSLIRP
        if (!m_slirp) return;
        serviceTimers();
        std::vector<PollEntry> entries;
        std::uint32_t timeout = 0;
        slirp_pollfds_fill(m_slirp, &timeout, &addPollFdThunk, &entries);
#ifndef _WIN32
        std::vector<pollfd> pollfds;
        pollfds.reserve(entries.size());
        for (const auto& entry : entries) {
            short events = 0;
            if (entry.events & SLIRP_POLL_IN) events |= POLLIN;
            if (entry.events & SLIRP_POLL_OUT) events |= POLLOUT;
            if (entry.events & SLIRP_POLL_PRI) events |= POLLPRI;
            pollfd pfd {};
            pfd.fd = entry.fd;
            pfd.events = events;
            pollfds.push_back(pfd);
        }
        const auto result = pollfds.empty() ? 0 : ::poll(pollfds.data(), pollfds.size(), 0);
        if (result >= 0) {
            for (std::size_t i = 0; i < pollfds.size(); ++i) {
                if (pollfds[i].revents & POLLIN) entries[i].revents |= SLIRP_POLL_IN;
                if (pollfds[i].revents & POLLOUT) entries[i].revents |= SLIRP_POLL_OUT;
                if (pollfds[i].revents & POLLPRI) entries[i].revents |= SLIRP_POLL_PRI;
                if (pollfds[i].revents & (POLLERR | POLLHUP | POLLNVAL)) entries[i].revents |= SLIRP_POLL_ERR;
            }
        }
        slirp_pollfds_poll(m_slirp, result < 0 ? 1 : 0, &getReventsThunk, &entries);
#else
        slirp_pollfds_poll(m_slirp, 0, &getReventsThunk, &entries);
#endif
#endif
    }

    bool transmitFrame(const QByteArray& frame)
    {
#if CUTEMAC_HAS_LIBSLIRP
        if (!m_slirp || frame.isEmpty()) return false;
        slirp_input(m_slirp, reinterpret_cast<const std::uint8_t*>(frame.constData()), frame.size());
        return true;
#else
        (void)frame;
        return false;
#endif
    }

    std::optional<QByteArray> receiveFrame()
    {
        if (m_receivedFrames.empty()) return std::nullopt;
        auto frame = std::move(m_receivedFrames.front());
        m_receivedFrames.pop_front();
        return frame;
    }

    void close()
    {
#if CUTEMAC_HAS_LIBSLIRP
        if (m_slirp) {
            slirp_cleanup(m_slirp);
            m_slirp = nullptr;
        }
#endif
        m_connected = false;
        m_receivedFrames.clear();
    }

    [[nodiscard]] bool connected() const { return m_connected; }

private:
#if CUTEMAC_HAS_LIBSLIRP
    struct Timer {
        SlirpTimerCb callback = nullptr;
        void* opaque = nullptr;
        bool active = false;
        std::int64_t expireMs = 0;
    };
    struct PollEntry {
        int fd = -1;
        int events = 0;
        int revents = 0;
    };
#endif

    void initialize()
    {
#if CUTEMAC_HAS_LIBSLIRP
        const auto hostIp = parseIpv4Address(m_configuration.hostIp, { 172, 16, 0, 1 });
        const auto guestIp = parseIpv4Address(m_configuration.guestIp, { 172, 16, 0, 2 });

        SlirpConfig cfg {};
        in_addr vnetwork {};
        in_addr vnetmask {};
        in_addr vhost {};
        in_addr vdhcpStart {};
        in_addr vnameserver {};
        vnetwork.s_addr = qToBigEndian(ipv4ToHostOrder({ hostIp[0], hostIp[1], hostIp[2], 0 }));
        vnetmask.s_addr = qToBigEndian(0xffffff00U);
        vhost.s_addr = qToBigEndian(ipv4ToHostOrder(hostIp));
        vdhcpStart.s_addr = qToBigEndian(ipv4ToHostOrder(guestIp));
        vnameserver.s_addr = vhost.s_addr;
        cfg.version = SLIRP_CONFIG_VERSION_MAX;
        cfg.restricted = 0;
        cfg.in_enabled = true;
        cfg.vnetwork = vnetwork;
        cfg.vnetmask = vnetmask;
        cfg.vhost = vhost;
        cfg.vdhcp_start = vdhcpStart;
        cfg.vnameserver = vnameserver;
        cfg.if_mru = static_cast<size_t>(m_configuration.mtu);
        cfg.if_mtu = static_cast<size_t>(m_configuration.mtu);
        cfg.disable_host_loopback = false;
        cfg.enable_emu = false;
        cfg.disable_dns = false;
        cfg.disable_dhcp = !m_configuration.dhcpEnabled;
        cfg.in6_enabled = false;

        m_callbacks.send_packet = &Impl::sendPacketThunk;
        m_callbacks.guest_error = &Impl::guestErrorThunk;
        m_callbacks.clock_get_ns = &Impl::clockGetNsThunk;
        m_callbacks.timer_new = &Impl::timerNewThunk;
        m_callbacks.timer_free = &Impl::timerFreeThunk;
        m_callbacks.timer_mod = &Impl::timerModThunk;
        m_callbacks.register_poll_fd = &Impl::registerPollFdThunk;
        m_callbacks.unregister_poll_fd = &Impl::unregisterPollFdThunk;
        m_callbacks.notify = &Impl::notifyThunk;
        m_slirp = slirp_new(&cfg, &m_callbacks, this);
        m_connected = m_slirp != nullptr;
#else
        m_connected = false;
#endif
    }

#if CUTEMAC_HAS_LIBSLIRP
    static ssize_t sendPacketThunk(const void* buf, size_t len, void* opaque)
    {
        return static_cast<Impl*>(opaque)->sendPacket(buf, len);
    }
    static void guestErrorThunk(const char*, void*) {}
    static std::int64_t clockGetNsThunk(void*)
    {
        return static_cast<std::int64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());
    }
    static void* timerNewThunk(SlirpTimerCb cb, void* cbOpaque, void* opaque)
    {
        auto* self = static_cast<Impl*>(opaque);
        for (auto& timer : self->m_timers) {
            if (timer.callback == nullptr) {
                timer.callback = cb;
                timer.opaque = cbOpaque;
                return &timer;
            }
        }
        return nullptr;
    }
    static void timerFreeThunk(void* timer, void*) { static_cast<Timer*>(timer)->callback = nullptr; }
    static void timerModThunk(void* timer, std::int64_t expireTime, void*)
    {
        auto* t = static_cast<Timer*>(timer);
        t->expireMs = expireTime;
        t->active = true;
    }
    static void registerPollFdThunk(int, void*) {}
    static void unregisterPollFdThunk(int, void*) {}
    static void notifyThunk(void*) {}

    static int addPollFdThunk(int fd, int events, void* opaque)
    {
        auto* entries = static_cast<std::vector<PollEntry>*>(opaque);
        PollEntry entry;
        entry.fd = fd;
        entry.events = events;
        entry.revents = 0;
        entries->push_back(entry);
        return static_cast<int>(entries->size() - 1);
    }
    static int getReventsThunk(int index, void* opaque)
    {
        auto* entries = static_cast<std::vector<PollEntry>*>(opaque);
        if (index < 0 || static_cast<std::size_t>(index) >= entries->size()) return 0;
        return (*entries)[static_cast<std::size_t>(index)].revents;
    }

    void serviceTimers()
    {
        const auto nowMs = clockGetNsThunk(nullptr) / 1000000;
        for (auto& timer : m_timers) {
            if (timer.active && timer.callback && timer.expireMs <= nowMs) {
                timer.active = false;
                timer.callback(timer.opaque);
            }
        }
    }

    ssize_t sendPacket(const void* buf, size_t len)
    {
        m_receivedFrames.emplace_back(static_cast<const char*>(buf), static_cast<qsizetype>(len));
        return static_cast<ssize_t>(len);
    }

    Slirp* m_slirp = nullptr;
    SlirpCb m_callbacks {};
    std::array<Timer, 8> m_timers {};
#endif

    SlirpEthernetConfiguration m_configuration;
    bool m_connected = false;
    std::deque<QByteArray> m_receivedFrames;
};

SlirpEthernetBackend::SlirpEthernetBackend(SlirpEthernetConfiguration configuration)
    : m_impl(std::make_unique<Impl>(std::move(configuration)))
{
}

SlirpEthernetBackend::~SlirpEthernetBackend() = default;

void SlirpEthernetBackend::poll()
{
    m_impl->poll();
}

bool SlirpEthernetBackend::transmitFrame(const QByteArray& frame)
{
    return m_impl->transmitFrame(frame);
}

std::optional<QByteArray> SlirpEthernetBackend::receiveFrame()
{
    return m_impl->receiveFrame();
}

void SlirpEthernetBackend::close()
{
    m_impl->close();
}

bool SlirpEthernetBackend::connected() const
{
    return m_impl->connected();
}

QString SlirpEthernetBackend::statusDetail() const
{
    if (m_impl->connected()) return {};
#if CUTEMAC_HAS_LIBSLIRP
    return QStringLiteral("SLIRP failed to start.");
#else
    return QStringLiteral("This build has no SLIRP support.");
#endif
}

} // namespace cutemac::devices::network
