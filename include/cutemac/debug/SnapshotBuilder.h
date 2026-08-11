#pragma once

#include <cstdint>
#include <functional>

#include <QPair>
#include <QString>

#include "cutemac/cpu/m68k/M68kCpuCore.h"
#include "cutemac/cpu/ppc/PpcCpuCore.h"
#include "cutemac/debug/MachineSnapshot.h"
#include "cutemac/devices/adb/AdbTransceiver.h"
#include "cutemac/devices/cuda/CudaController.h"
#include "cutemac/devices/iwm/IwmController.h"
#include "cutemac/devices/nubus/NuBusBus.h"
#include "cutemac/devices/rtc/MacRtc.h"
#include "cutemac/devices/scc/Z8530Scc.h"
#include "cutemac/devices/scsi/ncr53c94/Ncr53c94.h"
#include "cutemac/devices/scsi/ncr5380/Ncr5380.h"
#include "cutemac/devices/via6522/Via6522.h"

// Converts the per-device debug state the machines already expose into the
// machine-neutral snapshot model. Machines call these from debugSnapshot() so
// the wire format stays identical across chipsets.

namespace cutemac::debug {

// Side-effect-free physical read supplied by the machine.
using MemoryReader = std::function<std::uint8_t(std::uint32_t)>;
// Returns the rendered text and the instruction length in bytes.
using Disassembler = std::function<QPair<QString, int>(std::uint32_t)>;

inline constexpr int disassemblyWindowInstructions = 32;
inline constexpr int backtraceFrameLimit = 32;
inline constexpr int vectorTableEntries = 256;

[[nodiscard]] CpuSnapshot buildCpuSnapshot(const cpu::m68k::M68kCpuCore::RegisterSnapshot& registers,
    const QString& architecture, const MemoryReader& read8, const Disassembler& disassemble);

[[nodiscard]] CpuSnapshot buildCpuSnapshot(const cpu::ppc::PowerPc601Core::RegisterSnapshot& registers,
    const MemoryReader& read8, const Disassembler& disassemble);

[[nodiscard]] DeviceSnapshot viaSnapshot(const QString& id, const devices::via6522::Via6522::DebugState& state);
[[nodiscard]] DeviceSnapshot scsiSnapshot(const QString& id,
    const devices::scsi::ncr5380::Ncr5380::DebugState& state);
[[nodiscard]] DeviceSnapshot scsiSnapshot(const QString& id,
    const devices::scsi::ncr53c94::Ncr53c94::DebugState& state);
[[nodiscard]] DeviceSnapshot sccSnapshot(const QString& id,
    const devices::scc::Z8530Scc::DebugChannelState& state, bool interruptActive);
[[nodiscard]] DeviceSnapshot iwmSnapshot(const QString& id, const devices::iwm::IwmController::DebugState& state);
[[nodiscard]] DeviceSnapshot adbSnapshot(const QString& id, const devices::adb::AdbTransceiver::DebugState& state);
[[nodiscard]] DeviceSnapshot cudaSnapshot(const QString& id, const devices::cuda::CudaController::DebugState& state);
[[nodiscard]] DeviceSnapshot rtcSnapshot(const QString& id, const devices::rtc::MacRtc& rtc);
[[nodiscard]] QVector<DeviceSnapshot> nubusSnapshots(const devices::nubus::NuBusBus& bus);

// Decoded low-memory globals. Cheap, and a clobbered vector or a nonsense
// MemTop is visible at a glance without opening the RAM image.
[[nodiscard]] DeviceSnapshot lowMemorySnapshot(const MemoryReader& read8);

} // namespace cutemac::debug
