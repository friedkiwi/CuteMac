#include "cutemac/devices/network/PcapEthernetBackend.h"

#include "cutemac/devices/network/PcapRuntime.h"

#include <QtGlobal>

#include <array>
#include <deque>
#include <utility>

#ifndef CUTEMAC_HAS_PCAP
#define CUTEMAC_HAS_PCAP 0
#endif

#if CUTEMAC_HAS_PCAP
#include <pcap.h>
#endif

namespace cutemac::devices::network {

namespace {

constexpr int minimumEthernetFrameBytes = 60;
constexpr int captureSnapLengthBytes = 65536;

} // namespace

QString captureFilterExpression(const std::array<std::uint8_t, 6>& mac, bool promiscuous)
{
    if (promiscuous) return {};
    QString address;
    for (const auto octet : mac) {
        if (!address.isEmpty()) address.append(QLatin1Char(':'));
        address.append(QStringLiteral("%1").arg(octet, 2, 16, QLatin1Char('0')));
    }
    return QStringLiteral("ether dst %1 or ether broadcast or ether multicast").arg(address);
}

QByteArray padToMinimumFrame(const QByteArray& frame)
{
    if (frame.size() >= minimumEthernetFrameBytes) return frame;
    QByteArray padded = frame;
    padded.append(minimumEthernetFrameBytes - frame.size(), '\0');
    return padded;
}

class PcapEthernetBackend::Impl {
public:
    explicit Impl(PcapEthernetConfiguration configuration)
        : m_configuration(std::move(configuration))
    {
        open();
    }

    ~Impl() { close(); }

    void poll()
    {
#if CUTEMAC_HAS_PCAP
        if (m_handle == nullptr) return;
        const auto budget = m_configuration.maxFramesPerPoll > 0 ? m_configuration.maxFramesPerPoll : 1;
        for (int captured = 0; captured < budget; ++captured) {
            pcap_pkthdr* header = nullptr;
            const std::uint8_t* data = nullptr;
            const auto result = pcap_next_ex(m_handle, &header, &data);
            if (result != 1 || header == nullptr || data == nullptr) {
                // 0 is "nothing waiting" on a non-blocking handle; -2 cannot
                // happen on a live capture. Anything else took the link down.
                if (result < 0 && result != -2) {
                    m_status = QStringLiteral("Capture on '%1' stopped: %2")
                                   .arg(m_configuration.interfaceName,
                                       QString::fromLocal8Bit(pcap_geterr(m_handle)));
                    closeHandle();
                }
                return;
            }
            const auto length = static_cast<qsizetype>(header->caplen);
            if (length <= 0) continue;
            m_receivedFrames.emplace_back(reinterpret_cast<const char*>(data), length);
        }
#endif
    }

    bool transmitFrame(const QByteArray& frame)
    {
#if CUTEMAC_HAS_PCAP
        if (m_handle == nullptr || frame.isEmpty()) return false;
        const auto padded = padToMinimumFrame(frame);
        return pcap_sendpacket(m_handle,
                   reinterpret_cast<const std::uint8_t*>(padded.constData()),
                   static_cast<int>(padded.size()))
            == 0;
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

    void setStationAddress(const std::array<std::uint8_t, 6>& mac)
    {
        if (mac == m_stationAddress) return;
        m_stationAddress = mac;
        applyFilter();
    }

    void setPromiscuous(bool enabled)
    {
        if (enabled == m_promiscuous) return;
        m_promiscuous = enabled;
        applyFilter();
    }

    void close()
    {
        closeHandle();
        m_receivedFrames.clear();
    }

    [[nodiscard]] bool connected() const
    {
#if CUTEMAC_HAS_PCAP
        return m_handle != nullptr;
#else
        return false;
#endif
    }

    [[nodiscard]] QString statusDetail() const { return m_status; }

private:
    void closeHandle()
    {
#if CUTEMAC_HAS_PCAP
        if (m_handle != nullptr) {
            pcap_close(m_handle);
            m_handle = nullptr;
        }
#endif
    }

    void open()
    {
#if !CUTEMAC_HAS_PCAP
        m_status = QStringLiteral("This build has no bridged networking support.");
#else
        const auto& runtime = pcap::runtimeStatus();
        if (!runtime.available) {
            m_status = runtime.reason;
            return;
        }
        if (m_configuration.interfaceName.trimmed().isEmpty()) {
            m_status = QStringLiteral("No host network interface is selected for bridged networking.");
            return;
        }

        const auto device = m_configuration.interfaceName.toLocal8Bit();
        char errorBuffer[PCAP_ERRBUF_SIZE] = {};
        auto* handle = pcap_create(device.constData(), errorBuffer);
        if (handle == nullptr) {
            m_status = QStringLiteral("Host interface '%1' is not available: %2")
                           .arg(m_configuration.interfaceName, QString::fromLocal8Bit(errorBuffer));
            return;
        }

        pcap_set_snaplen(handle, captureSnapLengthBytes);
        pcap_set_promisc(handle, 1);
        pcap_set_timeout(handle, 1);
        // Without immediate mode the kernel batches frames until its buffer
        // fills, which a guest waiting on a single reply reads as a dead link.
        pcap_set_immediate_mode(handle, 1);

        const auto activated = pcap_activate(handle);
        if (activated < 0) {
            m_status = activationMessage(handle, activated);
            pcap_close(handle);
            return;
        }

        if (pcap_datalink(handle) != DLT_EN10MB) {
            m_status = QStringLiteral("Host interface '%1' is not Ethernet, so an emulated Ethernet card "
                                      "cannot bridge onto it.")
                           .arg(m_configuration.interfaceName);
            pcap_close(handle);
            return;
        }

        if (pcap_setnonblock(handle, 1, errorBuffer) != 0) {
            m_status = QStringLiteral("Host interface '%1' cannot be used without blocking: %2")
                           .arg(m_configuration.interfaceName, QString::fromLocal8Bit(errorBuffer));
            pcap_close(handle);
            return;
        }

        // Keeps our own transmissions from coming straight back as receives.
        // Windows has no equivalent and reports failure; Npcap does not loop
        // sent frames back in the first place.
        (void)pcap_setdirection(handle, PCAP_D_IN);

        m_handle = handle;
        m_status.clear();
        applyFilter();
#endif
    }

#if CUTEMAC_HAS_PCAP
    QString activationMessage(pcap_t* handle, int code) const
    {
        const auto detail = QString::fromLocal8Bit(pcap_geterr(handle));
        if (code == PCAP_ERROR_PERM_DENIED) {
#if defined(Q_OS_MACOS)
            return QStringLiteral("CuteMac cannot open /dev/bpf* for '%1'. Install Wireshark's ChmodBPF "
                                  "helper, or add your user to the access_bpf group, then restart CuteMac.")
                .arg(m_configuration.interfaceName);
#elif defined(Q_OS_WIN)
            return QStringLiteral("Npcap denied access to '%1'. Re-run the Npcap installer with "
                                  "\"Restrict Npcap driver's access to Administrators only\" unchecked, "
                                  "or start CuteMac as Administrator.")
                .arg(m_configuration.interfaceName);
#else
            return QStringLiteral("CuteMac lacks CAP_NET_RAW for '%1'. Grant it with: "
                                  "sudo setcap cap_net_raw,cap_net_admin+eip <path to CuteMac>")
                .arg(m_configuration.interfaceName);
#endif
        }
        if (code == PCAP_ERROR_NO_SUCH_DEVICE) {
            return QStringLiteral("Host interface '%1' is not present on this machine. Choose another "
                                  "interface, or switch this card to SLIRP.")
                .arg(m_configuration.interfaceName);
        }
        return QStringLiteral("Host interface '%1' could not be opened: %2")
            .arg(m_configuration.interfaceName, detail);
    }
#endif

    void applyFilter()
    {
#if CUTEMAC_HAS_PCAP
        if (m_handle == nullptr) return;
        const auto expression = captureFilterExpression(m_stationAddress, m_promiscuous);
        if (expression.isEmpty()) {
            // Promiscuous guest: drop any previous filter by installing one
            // that matches everything.
            bpf_program program {};
            if (pcap_compile(m_handle, &program, "", 1, PCAP_NETMASK_UNKNOWN) == 0) {
                pcap_setfilter(m_handle, &program);
                pcap_freecode(&program);
            }
            return;
        }
        const auto text = expression.toLatin1();
        bpf_program program {};
        if (pcap_compile(m_handle, &program, text.constData(), 1, PCAP_NETMASK_UNKNOWN) != 0) return;
        pcap_setfilter(m_handle, &program);
        pcap_freecode(&program);
#endif
    }

    PcapEthernetConfiguration m_configuration;
#if CUTEMAC_HAS_PCAP
    pcap_t* m_handle = nullptr;
#endif
    std::array<std::uint8_t, 6> m_stationAddress {};
    bool m_promiscuous = false;
    QString m_status;
    std::deque<QByteArray> m_receivedFrames;
};

PcapEthernetBackend::PcapEthernetBackend(PcapEthernetConfiguration configuration)
    : m_impl(std::make_unique<Impl>(std::move(configuration)))
{
}

PcapEthernetBackend::~PcapEthernetBackend() = default;

void PcapEthernetBackend::poll()
{
    m_impl->poll();
}

bool PcapEthernetBackend::transmitFrame(const QByteArray& frame)
{
    return m_impl->transmitFrame(frame);
}

std::optional<QByteArray> PcapEthernetBackend::receiveFrame()
{
    return m_impl->receiveFrame();
}

void PcapEthernetBackend::setStationAddress(const std::array<std::uint8_t, 6>& mac)
{
    m_impl->setStationAddress(mac);
}

void PcapEthernetBackend::setPromiscuous(bool enabled)
{
    m_impl->setPromiscuous(enabled);
}

void PcapEthernetBackend::close()
{
    m_impl->close();
}

bool PcapEthernetBackend::connected() const
{
    return m_impl->connected();
}

QString PcapEthernetBackend::statusDetail() const
{
    return m_impl->statusDetail();
}

} // namespace cutemac::devices::network
