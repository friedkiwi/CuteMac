#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>

namespace cutemac::core {

// Sparse 32-bit physical-address map. Direct pages are deliberately limited
// to side-effect-free memory; machines retain their normal bus decoder for
// devices, cross-page accesses, open bus, and bus-error behavior.
class PhysicalMemoryMap {
public:
    static constexpr std::uint32_t pageShift = 12;
    static constexpr std::uint32_t pageSize = 1U << pageShift;
    static constexpr std::uint32_t pageMask = pageSize - 1;

    void clear()
    {
        for (auto& directory : m_directories) {
            if (directory) std::fill_n(directory.get(), entriesPerDirectory, Page {});
        }
    }

    void mapReadWritePage(std::uint32_t address, std::uint8_t* bytes)
    {
        if ((address & pageMask) != 0 || bytes == nullptr) return;
        auto& page = pageForMapping(address);
        page.read = bytes;
        page.write = bytes;
    }

    void mapReadOnlyPage(std::uint32_t address, const std::uint8_t* bytes)
    {
        if ((address & pageMask) != 0 || bytes == nullptr) return;
        auto& page = pageForMapping(address);
        page.read = bytes;
        page.write = nullptr;
    }

    void mapReadOnlyMirrored(std::uint32_t address, std::uint32_t length,
        const std::uint8_t* bytes, std::uint32_t bytesLength)
    {
        if ((address & pageMask) != 0 || (length & pageMask) != 0 || bytes == nullptr
            || bytesLength == 0 || (bytesLength & pageMask) != 0) return;
        for (std::uint32_t offset = 0; offset < length; offset += pageSize) {
            mapReadOnlyPage(address + offset, bytes + (offset % bytesLength));
        }
    }

    [[nodiscard]] bool tryRead8(std::uint32_t address, std::uint8_t& value) const
    {
        const auto* bytes = pageFor(address).read;
        if (!bytes) return false;
        value = bytes[address & pageMask];
        return true;
    }

    [[nodiscard]] bool tryRead16(std::uint32_t address, std::uint16_t& value) const
    {
        if ((address & pageMask) > pageMask - 1) return false;
        const auto* bytes = pageFor(address).read;
        if (!bytes) return false;
        const auto offset = address & pageMask;
        value = static_cast<std::uint16_t>((static_cast<std::uint16_t>(bytes[offset]) << 8)
            | bytes[offset + 1]);
        return true;
    }

    [[nodiscard]] bool tryRead32(std::uint32_t address, std::uint32_t& value) const
    {
        if ((address & pageMask) > pageMask - 3) return false;
        const auto* bytes = pageFor(address).read;
        if (!bytes) return false;
        const auto offset = address & pageMask;
        value = (static_cast<std::uint32_t>(bytes[offset]) << 24)
            | (static_cast<std::uint32_t>(bytes[offset + 1]) << 16)
            | (static_cast<std::uint32_t>(bytes[offset + 2]) << 8)
            | bytes[offset + 3];
        return true;
    }

    [[nodiscard]] bool tryWrite8(std::uint32_t address, std::uint8_t value)
    {
        auto* bytes = pageFor(address).write;
        if (!bytes) return false;
        bytes[address & pageMask] = value;
        return true;
    }

    [[nodiscard]] bool tryWrite16(std::uint32_t address, std::uint16_t value)
    {
        if ((address & pageMask) > pageMask - 1) return false;
        auto* bytes = pageFor(address).write;
        if (!bytes) return false;
        const auto offset = address & pageMask;
        bytes[offset] = static_cast<std::uint8_t>(value >> 8);
        bytes[offset + 1] = static_cast<std::uint8_t>(value);
        return true;
    }

    [[nodiscard]] bool tryWrite32(std::uint32_t address, std::uint32_t value)
    {
        if ((address & pageMask) > pageMask - 3) return false;
        auto* bytes = pageFor(address).write;
        if (!bytes) return false;
        const auto offset = address & pageMask;
        bytes[offset] = static_cast<std::uint8_t>(value >> 24);
        bytes[offset + 1] = static_cast<std::uint8_t>(value >> 16);
        bytes[offset + 2] = static_cast<std::uint8_t>(value >> 8);
        bytes[offset + 3] = static_cast<std::uint8_t>(value);
        return true;
    }

private:
    static constexpr std::uint32_t directoryShift = 22;
    static constexpr std::uint32_t directoryCount = 1U << (32 - directoryShift);
    static constexpr std::uint32_t entriesPerDirectory = 1U << (directoryShift - pageShift);
    static constexpr std::uint32_t directoryPageMask = entriesPerDirectory - 1;

    struct Page {
        const std::uint8_t* read = nullptr;
        std::uint8_t* write = nullptr;
    };

    [[nodiscard]] const Page& pageFor(std::uint32_t address) const
    {
        const auto& directory = m_directories[address >> directoryShift];
        if (!directory) return m_unmappedPage;
        return directory[(address >> pageShift) & directoryPageMask];
    }

    [[nodiscard]] Page& pageFor(std::uint32_t address)
    {
        auto& directory = m_directories[address >> directoryShift];
        if (!directory) return m_unmappedPage;
        return directory[(address >> pageShift) & directoryPageMask];
    }

    [[nodiscard]] Page& pageForMapping(std::uint32_t address)
    {
        auto& directory = m_directories[address >> directoryShift];
        if (!directory) directory = std::make_unique<Page[]>(entriesPerDirectory);
        return directory[(address >> pageShift) & directoryPageMask];
    }

    std::array<std::unique_ptr<Page[]>, directoryCount> m_directories;
    Page m_unmappedPage;
};

} // namespace cutemac::core
