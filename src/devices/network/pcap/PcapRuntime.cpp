#include "cutemac/devices/network/PcapRuntime.h"

#ifndef CUTEMAC_HAS_PCAP
#define CUTEMAC_HAS_PCAP 0
#endif

#if CUTEMAC_HAS_PCAP
#include <pcap.h>
#ifdef _WIN32
#include <windows.h>

#include <string>
#endif
#endif

namespace cutemac::devices::network::pcap {

namespace {

#if CUTEMAC_HAS_PCAP && defined(_WIN32)

// Npcap installs wpcap.dll into System32 only when the user ticked "WinPcap
// API-compatible mode"; otherwise it lands in System32\Npcap. Search both, and
// never fall back to the application directory: a wpcap.dll sitting next to
// CuteMac.exe would be a redistributed copy we must not load.
bool loadWpcap()
{
    static const bool loaded = [] {
        SetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_SYSTEM32 | LOAD_LIBRARY_SEARCH_USER_DIRS);
        if (LoadLibraryExW(L"wpcap.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32) != nullptr) return true;

        wchar_t systemDirectory[MAX_PATH] = {};
        const auto length = GetSystemDirectoryW(systemDirectory, MAX_PATH);
        if (length == 0 || length >= MAX_PATH) return false;

        std::wstring npcapDirectory(systemDirectory, length);
        npcapDirectory.append(L"\\Npcap");
        if (AddDllDirectory(npcapDirectory.c_str()) == nullptr) return false;

        npcapDirectory.append(L"\\wpcap.dll");
        return LoadLibraryExW(npcapDirectory.c_str(), nullptr, LOAD_LIBRARY_SEARCH_DEFAULT_DIRS) != nullptr;
    }();
    return loaded;
}

#endif

RuntimeStatus resolve()
{
#if !CUTEMAC_HAS_PCAP
    return { false, QStringLiteral("This build has no bridged networking support."), {} };
#else
#ifdef _WIN32
    if (!loadWpcap()) {
        return { false,
            QStringLiteral("Bridged networking needs Npcap, which is not installed. Download it from "
                           "https://npcap.com and reopen this dialog. CuteMac does not bundle Npcap, and "
                           "does not need WinPcap API-compatible mode."),
            {} };
    }
#endif
    return { true, {}, QString::fromLatin1(pcap_lib_version()) };
#endif
}

} // namespace

const RuntimeStatus& runtimeStatus()
{
    static const RuntimeStatus status = resolve();
    return status;
}

} // namespace cutemac::devices::network::pcap
