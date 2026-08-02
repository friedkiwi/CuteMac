#include "cutemac/devices/bus/ByteWideMmioAdapter.h"

#include <algorithm>

namespace cutemac::devices::bus {

ByteWideMmioAdapter::ByteWideMmioAdapter(Wiring wiring, ReadCallback read, WriteCallback write)
    : m_wiring(wiring)
    , m_read(std::move(read))
    , m_write(std::move(write))
{
}

core::BusResponse ByteWideMmioAdapter::access(std::uint32_t offset, const core::BusTransaction& transaction)
{
    const auto accessBytes = std::clamp<unsigned>(transaction.size, 1, 4);
    const bool write = transaction.kind == core::BusAccessKind::DataWrite
        || transaction.kind == core::BusAccessKind::DebugWrite;
    if (write) {
        if (m_write) m_write(offset, selectWrite(transaction.value, accessBytes));
        return {};
    }

    const auto value = m_read ? m_read(offset, transaction.sideEffects) : m_wiring.openBus;
    return {placeRead(value, accessBytes)};
}

unsigned ByteWideMmioAdapter::laneShift(unsigned accessBytes, bool mostSignificant)
{
    return accessBytes == 1 || !mostSignificant ? 0 : (accessBytes - 1) * 8;
}

std::uint32_t ByteWideMmioAdapter::placeRead(std::uint8_t value, unsigned accessBytes) const
{
    if (accessBytes == 1) return value;
    if (m_wiring.read == ReadWiring::Replicate) {
        std::uint32_t result = 0;
        for (unsigned byte = 0; byte < accessBytes; ++byte) result = (result << 8) | value;
        return result;
    }
    return static_cast<std::uint32_t>(value)
        << laneShift(accessBytes, m_wiring.read == ReadWiring::MostSignificantLane);
}

std::uint8_t ByteWideMmioAdapter::selectWrite(std::uint32_t value, unsigned accessBytes) const
{
    return static_cast<std::uint8_t>(value
        >> laneShift(accessBytes, m_wiring.write == WriteWiring::MostSignificantLane));
}

} // namespace cutemac::devices::bus
