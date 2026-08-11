#pragma once

#include <QString>

namespace cutemac::devices::network::pcap {

struct RuntimeStatus {
    bool available = false;
    // User-facing explanation, empty only when `available` is true.
    QString reason;
    // pcap_lib_version(), for logs and the configuration UI.
    QString version;
};

// Resolves the capture library once per process and caches the result.
//
// On Windows this is a real question at runtime: wpcap.dll ships with Npcap,
// which the user installs separately and which CuteMac must never redistribute.
// The library is delay-loaded, so nothing here touches a pcap symbol until the
// probe has found the DLL.
[[nodiscard]] const RuntimeStatus& runtimeStatus();

} // namespace cutemac::devices::network::pcap
