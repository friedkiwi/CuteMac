// DP8390 behavior in this file is derived from MAME's BSD-3-Clause
// src/devices/machine/dp8390.cpp and NuBus Apple Ethernet card mapping from
// src/devices/bus/nubus/nubus_asntmc3b.cpp.

#include "cutemac/devices/network/nubus/AppleNuBusEthernetCard.h"

#include <QFile>

#include <algorithm>
#include <cstring>
#include <functional>

namespace cutemac::devices::network::nubus {

namespace {

constexpr std::uint32_t localMask = 0x000fffffU;
constexpr std::uint32_t declarationRomBase = 0x00ff8000U;
constexpr std::uint32_t packetRamBase = 0x000d0000U;
constexpr std::uint32_t dp8390Base = 0x000e0000U;
constexpr std::uint32_t dp8390Limit = dp8390Base + 0x40U;
constexpr std::uint8_t isrReset = 0x80U;
constexpr std::uint8_t isrPacketReceived = 0x01U;
constexpr std::uint8_t isrPacketTransmitted = 0x02U;
constexpr std::uint8_t isrTransmitError = 0x08U;
constexpr std::uint8_t isrReceiveOverflow = 0x10U;
constexpr std::uint8_t isrRemoteDmaComplete = 0x40U;

std::uint16_t swap16(std::uint16_t value)
{
    return static_cast<std::uint16_t>((value >> 8) | (value << 8));
}

std::array<std::uint8_t, 6> parseMacAddress(const QString& text)
{
    std::array<std::uint8_t, 6> result { 0x02, 0x00, 0x1b, 0x00, 0x00, 0x01 };
    const auto parts = text.split(QLatin1Char(':'));
    if (parts.size() != 6) return result;
    for (int index = 0; index < 6; ++index) {
        bool ok = false;
        const auto value = parts[index].toUInt(&ok, 16);
        if (!ok || value > 0xffU) return result;
        result[static_cast<std::size_t>(index)] = static_cast<std::uint8_t>(value);
    }
    result[0] &= 0xfeU;
    if (result == std::array<std::uint8_t, 6> {}) result[0] = 0x02U;
    return result;
}

} // namespace

class AppleNuBusEthernetCard::Dp8390 {
public:
    using ReadMemory = std::function<std::uint8_t(std::uint32_t)>;
    using WriteMemory = std::function<void(std::uint32_t, std::uint8_t)>;
    using Transmit = std::function<bool(const QByteArray&)>;
    using Irq = std::function<void(bool)>;

    Dp8390(ReadMemory readMemory, WriteMemory writeMemory, Transmit transmit, Irq irq)
        : m_readMemory(std::move(readMemory))
        , m_writeMemory(std::move(writeMemory))
        , m_transmit(std::move(transmit))
        , m_irq(std::move(irq))
    {
        reset();
    }

    void reset()
    {
        m_regs = {};
        m_regs.cr = 0x21U;
        m_regs.isr = isrReset;
        m_regs.dcr = 0x04U;
        m_reset = true;
        m_rdmaActive = 0;
        checkIrq();
    }

    void setInitialMac(const std::array<std::uint8_t, 6>& mac)
    {
        std::copy(mac.cbegin(), mac.cend(), std::begin(m_regs.par));
    }

    void setFilterChanged(std::function<void()> callback)
    {
        m_filterChanged = std::move(callback);
    }

    [[nodiscard]] std::array<std::uint8_t, 6> stationAddress() const
    {
        std::array<std::uint8_t, 6> mac {};
        std::copy(std::begin(m_regs.par), std::end(m_regs.par), mac.begin());
        return mac;
    }

    [[nodiscard]] bool promiscuous() const { return (m_regs.rcr & 0x10U) != 0; }

    std::uint8_t csRead(std::uint8_t offset) const
    {
        switch ((offset & 0x0fU) | (m_regs.cr & 0xc0U)) {
        case 0x00:
        case 0x40:
        case 0x80:
        case 0xc0:
            return m_regs.cr;
        case 0x01: return static_cast<std::uint8_t>(m_regs.clda);
        case 0x02: return static_cast<std::uint8_t>(m_regs.clda >> 8);
        case 0x03: return m_regs.bnry;
        case 0x04: return m_regs.tsr;
        case 0x05: return m_regs.ncr;
        case 0x06: return m_regs.fifo;
        case 0x07: return m_regs.isr;
        case 0x08: return static_cast<std::uint8_t>(m_regs.crda);
        case 0x09: return static_cast<std::uint8_t>(m_regs.crda >> 8);
        case 0x0c: return m_regs.rsr;
        case 0x0d: return m_regs.cntr0;
        case 0x0e: return m_regs.cntr1;
        case 0x0f: return m_regs.cntr2;
        case 0x41:
        case 0x42:
        case 0x43:
        case 0x44:
        case 0x45:
        case 0x46:
            return m_regs.par[(offset & 0x07U) - 1U];
        case 0x47: return m_regs.curr;
        case 0x48:
        case 0x49:
        case 0x4a:
        case 0x4b:
        case 0x4c:
        case 0x4d:
        case 0x4e:
        case 0x4f:
            return m_regs.mar[offset & 0x07U];
        case 0x81: return m_regs.pstart;
        case 0x82: return m_regs.pstop;
        case 0x83: return m_regs.rnpp;
        case 0x84: return m_regs.tpsr;
        case 0x85: return m_regs.lnpp;
        case 0x86: return static_cast<std::uint8_t>(m_regs.ac >> 8);
        case 0x87: return static_cast<std::uint8_t>(m_regs.ac);
        case 0x8c: return m_regs.rcr;
        case 0x8d: return m_regs.tcr;
        case 0x8e: return m_regs.dcr;
        case 0x8f: return m_regs.imr;
        default: return 0;
        }
    }

    void csWrite(std::uint8_t offset, std::uint8_t data)
    {
        switch ((offset & 0x0fU) | (m_regs.cr & 0xc0U)) {
        case 0x00:
        case 0x40:
        case 0x80:
        case 0xc0:
            setCommand(data);
            break;
        case 0x01: m_regs.pstart = data; break;
        case 0x02: m_regs.pstop = data; break;
        case 0x03: m_regs.bnry = data; break;
        case 0x04: m_regs.tpsr = data; break;
        case 0x05: m_regs.tbcr = static_cast<std::uint16_t>((m_regs.tbcr & 0xff00U) | data); break;
        case 0x06: m_regs.tbcr = static_cast<std::uint16_t>((m_regs.tbcr & 0x00ffU) | (data << 8)); break;
        case 0x07:
            m_regs.isr &= static_cast<std::uint8_t>(~data);
            checkIrq();
            break;
        case 0x08:
            m_regs.rsar = static_cast<std::uint16_t>((m_regs.rsar & 0xff00U) | data);
            m_regs.crda = m_regs.rsar;
            break;
        case 0x09:
            m_regs.rsar = static_cast<std::uint16_t>((m_regs.rsar & 0x00ffU) | (data << 8));
            m_regs.crda = m_regs.rsar;
            break;
        case 0x0a: m_regs.rbcr = static_cast<std::uint16_t>((m_regs.rbcr & 0xff00U) | data); break;
        case 0x0b: m_regs.rbcr = static_cast<std::uint16_t>((m_regs.rbcr & 0x00ffU) | (data << 8)); break;
        case 0x0c:
            m_regs.rcr = data;
            notifyFilterChanged();
            break;
        case 0x0d: m_regs.tcr = data; break;
        case 0x0e: m_regs.dcr = data; break;
        case 0x0f:
            m_regs.imr = data;
            checkIrq();
            break;
        case 0x41:
        case 0x42:
        case 0x43:
        case 0x44:
        case 0x45:
        case 0x46:
            m_regs.par[(offset & 0x07U) - 1U] = data;
            notifyFilterChanged();
            break;
        case 0x47: m_regs.curr = data; break;
        case 0x48:
        case 0x49:
        case 0x4a:
        case 0x4b:
        case 0x4c:
        case 0x4d:
        case 0x4e:
        case 0x4f:
            m_regs.mar[offset & 0x07U] = data;
            break;
        case 0x81: m_regs.clda = static_cast<std::uint16_t>((m_regs.clda & 0xff00U) | data); break;
        case 0x82: m_regs.clda = static_cast<std::uint16_t>((m_regs.clda & 0x00ffU) | (data << 8)); break;
        case 0x83: m_regs.rnpp = data; break;
        case 0x85: m_regs.lnpp = data; break;
        case 0x86: m_regs.ac = static_cast<std::uint16_t>((m_regs.ac & 0x00ffU) | (data << 8)); break;
        case 0x87: m_regs.ac = static_cast<std::uint16_t>((m_regs.ac & 0xff00U) | data); break;
        default: break;
        }
    }

    std::uint16_t remoteRead()
    {
        const auto high16 = (m_regs.dcr & 0x04U) != 0 ? static_cast<std::uint32_t>(m_regs.rsar) << 16 : 0U;
        if ((m_regs.dcr & 0x01U) != 0) {
            m_regs.crda &= 0xfffeU;
            std::uint16_t data = m_readMemory(high16 + m_regs.crda++);
            data |= static_cast<std::uint16_t>(m_readMemory(high16 + m_regs.crda++)) << 8;
            m_regs.rbcr -= std::min<std::uint16_t>(m_regs.rbcr, 2);
            checkDmaComplete();
            return byteOrdered(data);
        }
        const auto data = m_readMemory(high16 + m_regs.crda++);
        if (m_regs.rbcr != 0) --m_regs.rbcr;
        checkDmaComplete();
        return data;
    }

    void remoteWrite(std::uint16_t data)
    {
        const auto high16 = (m_regs.dcr & 0x04U) != 0 ? static_cast<std::uint32_t>(m_regs.rsar) << 16 : 0U;
        if ((m_regs.dcr & 0x01U) != 0) {
            data = byteOrdered(data);
            m_regs.crda &= 0xfffeU;
            m_writeMemory(high16 + m_regs.crda++, static_cast<std::uint8_t>(data));
            m_writeMemory(high16 + m_regs.crda++, static_cast<std::uint8_t>(data >> 8));
            m_regs.rbcr -= std::min<std::uint16_t>(m_regs.rbcr, 2);
            checkDmaComplete();
        } else {
            m_writeMemory(high16 + m_regs.crda++, static_cast<std::uint8_t>(data));
            if (m_regs.rbcr != 0) --m_regs.rbcr;
            checkDmaComplete();
        }
    }

    void receiveFrame(const QByteArray& frame)
    {
        if (frame.isEmpty()) return;
        receive(reinterpret_cast<const std::uint8_t*>(frame.constData()), frame.size());
    }

private:
    struct Registers {
        std::uint8_t cr = 0;
        std::uint16_t clda = 0;
        std::uint8_t pstart = 0;
        std::uint8_t pstop = 0;
        std::uint8_t bnry = 0;
        std::uint8_t tsr = 0;
        std::uint8_t tpsr = 0;
        std::uint8_t ncr = 0;
        std::uint8_t fifo = 0;
        std::uint16_t tbcr = 0;
        std::uint8_t isr = 0;
        std::uint16_t crda = 0;
        std::uint16_t rsar = 0;
        std::uint16_t rbcr = 0;
        std::uint8_t rsr = 0;
        std::uint8_t rcr = 0;
        std::uint8_t cntr0 = 0;
        std::uint8_t tcr = 0;
        std::uint8_t cntr1 = 0;
        std::uint8_t dcr = 0;
        std::uint8_t cntr2 = 0;
        std::uint8_t imr = 0;
        std::uint8_t par[6] {};
        std::uint8_t curr = 0;
        std::uint8_t mar[8] {};
        std::uint8_t rnpp = 0;
        std::uint8_t lnpp = 0;
        std::uint16_t ac = 0;
    };

    std::uint16_t byteOrdered(std::uint16_t value) const
    {
        return (m_regs.dcr & 0x03U) == 0x03U ? swap16(value) : value;
    }

    bool loopback() const
    {
        return (m_regs.dcr & 0x08U) == 0 && (m_regs.tcr & 0x06U) != 0;
    }

    void setCommand(std::uint8_t command)
    {
        const bool wasStarted = (m_regs.cr & 0x03U) == 0x02U;
        m_regs.cr = command;
        if ((command & 0x01U) != 0 && wasStarted) {
            stop();
            return;
        }
        if ((command & 0x03U) == 0x02U) {
            m_reset = false;
            m_regs.isr &= ~isrReset;
        }
        if ((command & 0x20U) != 0) m_rdmaActive = 0;
        if (m_reset) return;
        if ((command & 0x04U) != 0) transmitPacket();
        if ((command & 0x38U) == 0x08U) {
            m_rdmaActive = 1;
            checkDmaComplete();
        }
        if ((command & 0x38U) == 0x10U) m_rdmaActive = 2;
    }

    void stop()
    {
        m_regs.isr = isrReset;
        m_regs.cr |= 0x01U;
        m_reset = true;
        m_irq(false);
    }

    void checkDmaComplete()
    {
        if (m_regs.rbcr != 0) return;
        m_regs.isr |= isrRemoteDmaComplete;
        m_rdmaActive = 0;
        m_regs.cr |= 0x20U;
        checkIrq();
    }

    void checkIrq()
    {
        m_irq((m_regs.imr & m_regs.isr & 0x7fU) != 0);
    }

    void transmitPacket()
    {
        if (m_reset) return;
        m_regs.tsr = 0;
        if (m_regs.tbcr == 0) {
            m_regs.tsr = 0x01U;
            m_regs.cr &= ~0x04U;
            return;
        }
        QByteArray frame;
        frame.resize(static_cast<qsizetype>(m_regs.tbcr));
        const auto base = static_cast<std::uint32_t>(m_regs.tpsr) << 8;
        for (std::uint16_t index = 0; index < m_regs.tbcr; ++index) {
            frame[static_cast<qsizetype>(index)] = static_cast<char>(m_readMemory(base + index));
        }

        if (loopback()) {
            receive(reinterpret_cast<const std::uint8_t*>(frame.constData()), frame.size());
            m_regs.tsr = 0x01U;
            m_regs.isr |= isrPacketTransmitted;
        } else if (m_transmit(frame)) {
            m_regs.tsr = 0x01U;
            m_regs.isr |= isrPacketTransmitted;
        } else {
            m_regs.tsr = 0x08U;
            m_regs.isr |= isrTransmitError;
        }
        m_regs.cr &= ~0x04U;
        checkIrq();
    }

    void receiveOverflow()
    {
        m_regs.rsr = 0x10U;
        m_regs.isr |= isrReceiveOverflow;
        ++m_regs.cntr2;
        checkIrq();
    }

    void receive(const std::uint8_t* bytes, qsizetype length)
    {
        if (m_reset || length < 6 || m_regs.pstart == m_regs.pstop) return;
        if ((bytes[0] & 1U) != 0) {
            if (std::memcmp(bytes, "\xff\xff\xff\xff\xff\xff", 6) == 0) {
                if ((m_regs.rcr & 0x04U) == 0) return;
            } else {
                if ((m_regs.rcr & 0x08U) == 0) return;
                // Multicast hash filtering is rarely relevant for bring-up.
                // Accept enabled multicast rather than dropping useful traffic.
            }
            m_regs.rsr = 0x20U;
        } else if ((m_regs.rcr & 0x10U) != 0) {
            m_regs.rsr = 0;
        } else if (std::memcmp(m_regs.par, bytes, 6) != 0) {
            return;
        } else {
            m_regs.rsr = 0;
        }

        std::uint16_t start = static_cast<std::uint16_t>(m_regs.curr) << 8;
        if (m_regs.curr == m_regs.pstop) start = static_cast<std::uint16_t>(m_regs.pstart) << 8;
        std::uint16_t offset = start + 4U;
        auto storedLength = static_cast<std::uint16_t>(std::min<qsizetype>(length, 0xffff));
        for (std::uint16_t index = 0; index < storedLength; ++index) {
            m_writeMemory(offset++, bytes[index]);
            if ((offset & 0xffU) == 0) {
                if ((offset >> 8) == m_regs.pstop) offset = static_cast<std::uint16_t>(m_regs.pstart) << 8;
                if ((offset >> 8) == m_regs.bnry) {
                    receiveOverflow();
                    return;
                }
            }
        }
        while (storedLength < 60) {
            m_writeMemory(offset++, 0);
            ++storedLength;
        }

        m_regs.rsr |= 0x01U;
        m_regs.isr |= isrPacketReceived;
        m_regs.curr = static_cast<std::uint8_t>((offset >> 8) + ((offset & 0xffU) != 0 ? 1 : 0));
        if (m_regs.curr == m_regs.pstop) m_regs.curr = m_regs.pstart;
        const auto headerLength = static_cast<std::uint16_t>(storedLength + 4U);
        m_writeMemory(start, m_regs.rsr);
        m_writeMemory(start + 1U, m_regs.curr);
        m_writeMemory(start + 2U, static_cast<std::uint8_t>(headerLength));
        m_writeMemory(start + 3U, static_cast<std::uint8_t>(headerLength >> 8));
        checkIrq();
    }

    ReadMemory m_readMemory;
    WriteMemory m_writeMemory;
    void notifyFilterChanged() const
    {
        if (m_filterChanged) m_filterChanged();
    }

    Transmit m_transmit;
    Irq m_irq;
    std::function<void()> m_filterChanged;
    Registers m_regs;
    bool m_reset = false;
    int m_rdmaActive = 0;
};

AppleNuBusEthernetCard::AppleNuBusEthernetCard(QString macAddress,
    std::unique_ptr<network::PacketNetworkBackend> backend)
    : m_macAddress(std::move(macAddress))
    , m_backend(std::move(backend))
    , m_packetRam(packetRamBytes, 0)
{
    m_dp8390 = std::make_unique<Dp8390>(
        [this](std::uint32_t offset) { return readPacketRam(offset); },
        [this](std::uint32_t offset, std::uint8_t value) { writePacketRam(offset, value); },
        [this](const QByteArray& frame) { return transmitFrame(frame); },
        [this](bool asserted) { setIrq(asserted); });
    m_dp8390->setInitialMac(macAddressBytes());
    m_dp8390->setFilterChanged([this]() { publishStationFilter(); });
    reset();
}

AppleNuBusEthernetCard::~AppleNuBusEthernetCard() = default;

QString AppleNuBusEthernetCard::id() const
{
    return QStringLiteral("nubus-network-apple-ethernet");
}

bool AppleNuBusEthernetCard::loadDeclarationRom(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return false;
    const auto bytes = file.readAll();
    if (bytes.size() != declarationRomBytes) return false;
    if (static_cast<std::uint8_t>(bytes.back()) != 0xa5U) return false;

    m_declarationRom.fill(static_cast<char>(0xff), mappedDeclarationRomBytes);
    for (qsizetype index = 0; index + 1 < bytes.size(); index += 2) {
        const auto mapped = index * 2;
        m_declarationRom[mapped] = bytes[index];
        m_declarationRom[mapped + 2] = bytes[index + 1];
    }
    return true;
}

void AppleNuBusEthernetCard::reset()
{
    std::fill(m_packetRam.begin(), m_packetRam.end(), 0);
    m_dp8390->reset();
    m_dp8390->setInitialMac(macAddressBytes());
    publishStationFilter();
    setIrq(false);
}

void AppleNuBusEthernetCard::tick(std::uint64_t)
{
    if (!m_backend) return;
    m_backend->poll();
    while (auto frame = m_backend->receiveFrame()) {
        m_dp8390->receiveFrame(*frame);
    }
}

std::uint8_t AppleNuBusEthernetCard::read8(std::uint32_t offset)
{
    const auto local = offset & 0x00ffffffU;
    if (declarationRomLoaded() && local >= declarationRomBase
        && local < declarationRomBase + mappedDeclarationRomBytes) {
        return static_cast<std::uint8_t>(m_declarationRom[static_cast<qsizetype>(local - declarationRomBase)]);
    }
    const auto standardLocal = local & localMask;
    if (standardLocal >= packetRamBase && standardLocal < packetRamBase + packetRamBytes) {
        return readPacketRam(standardLocal - packetRamBase);
    }
    if (standardLocal >= dp8390Base && standardLocal < dp8390Limit) {
        return readRegisterByte(standardLocal);
    }
    return 0xffU;
}

std::uint16_t AppleNuBusEthernetCard::read16(std::uint32_t offset)
{
    const auto standardLocal = offset & localMask;
    if (standardLocal >= dp8390Base && standardLocal < dp8390Limit) {
        return readRegisterWord(standardLocal);
    }
    return NuBusCard::read16(offset);
}

std::uint32_t AppleNuBusEthernetCard::read32(std::uint32_t offset)
{
    const auto standardLocal = offset & localMask;
    if (standardLocal >= dp8390Base && standardLocal < dp8390Limit) {
        return static_cast<std::uint32_t>(readRegisterByte(standardLocal)) << 24;
    }
    return NuBusCard::read32(offset);
}

void AppleNuBusEthernetCard::write8(std::uint32_t offset, std::uint8_t value)
{
    const auto standardLocal = offset & localMask;
    if (standardLocal >= packetRamBase && standardLocal < packetRamBase + packetRamBytes) {
        writePacketRam(standardLocal - packetRamBase, value);
    } else if (standardLocal >= dp8390Base && standardLocal < dp8390Limit) {
        writeRegisterByte(standardLocal, value);
    }
}

void AppleNuBusEthernetCard::write16(std::uint32_t offset, std::uint16_t value)
{
    const auto standardLocal = offset & localMask;
    if (standardLocal >= dp8390Base && standardLocal < dp8390Limit) {
        writeRegisterWord(standardLocal, value);
        return;
    }
    NuBusCard::write16(offset, value);
}

void AppleNuBusEthernetCard::write32(std::uint32_t offset, std::uint32_t value)
{
    const auto standardLocal = offset & localMask;
    if (standardLocal >= dp8390Base && standardLocal < dp8390Limit) {
        writeRegisterByte(standardLocal, static_cast<std::uint8_t>(value >> 24));
        return;
    }
    NuBusCard::write32(offset, value);
}

std::uint8_t AppleNuBusEthernetCard::packetRamByte(std::uint16_t offset) const
{
    return readPacketRam(offset);
}

std::uint8_t AppleNuBusEthernetCard::readPacketRam(std::uint32_t offset) const
{
    return static_cast<std::uint8_t>(m_packetRam[static_cast<qsizetype>(offset & 0xffffU)]);
}

void AppleNuBusEthernetCard::writePacketRam(std::uint32_t offset, std::uint8_t value)
{
    m_packetRam[static_cast<qsizetype>(offset & 0xffffU)] = static_cast<char>(value);
}

std::uint8_t AppleNuBusEthernetCard::readRegisterByte(std::uint32_t local)
{
    if ((local & 3U) != 0) return 0xffU;
    const auto index = static_cast<std::uint8_t>((local - dp8390Base) >> 2);
    return m_dp8390->csRead(static_cast<std::uint8_t>(0x0fU - index));
}

std::uint16_t AppleNuBusEthernetCard::readRegisterWord(std::uint32_t local)
{
    if ((local & 2U) != 0) return 0xffffU;
    return m_dp8390->remoteRead();
}

void AppleNuBusEthernetCard::writeRegisterByte(std::uint32_t local, std::uint8_t value)
{
    if ((local & 3U) != 0) return;
    const auto index = static_cast<std::uint8_t>((local - dp8390Base) >> 2);
    m_dp8390->csWrite(static_cast<std::uint8_t>(0x0fU - index), value);
}

void AppleNuBusEthernetCard::writeRegisterWord(std::uint32_t local, std::uint16_t value)
{
    if ((local & 2U) != 0) return;
    m_dp8390->remoteWrite(value);
}

bool AppleNuBusEthernetCard::transmitFrame(const QByteArray& frame)
{
    if (!m_backend || !m_backend->connected()) return false;
    return m_backend->transmitFrame(frame);
}

void AppleNuBusEthernetCard::publishStationFilter()
{
    if (!m_backend) return;
    m_backend->setStationAddress(m_dp8390->stationAddress());
    m_backend->setPromiscuous(m_dp8390->promiscuous());
}

std::array<std::uint8_t, 6> AppleNuBusEthernetCard::macAddressBytes() const
{
    return parseMacAddress(m_macAddress);
}

} // namespace cutemac::devices::network::nubus
