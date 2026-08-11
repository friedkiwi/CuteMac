#pragma once

#include <cstdint>

#include <QByteArray>
#include <QMap>
#include <QPair>
#include <QString>
#include <QStringList>
#include <QVector>

#include "cutemac/devices/video/VideoFrame.h"

namespace cutemac::debug {

// Machine-neutral, frontend-neutral capture of everything a panic dump needs.
// Machines fill it in through IMachine::debugSnapshot(); the panic archive
// serializes it, and SnapshotMachine reconstructs a read-only debug target
// from it. Keep Qt value types only: no CPU, machine, or widget types.

inline constexpr int machineSnapshotSchemaVersion = 1;

struct MemoryRegion {
    QString name;                 // "ram", "rom", "vram-slot9"
    QString kind;                 // ram | rom | vram | io | mirror | open-bus
    std::uint32_t base = 0;
    std::uint32_t length = 0;
    // Width of the address window that decodes to this region, when the machine
    // mirrors it. ROM answers across a whole 256 MB window on the II family, so
    // a dump that only mapped `length` bytes leaves the PC reading open bus.
    // Zero means the region decodes exactly its own length.
    std::uint32_t decodeLength = 0;
    bool readable = true;
    bool writable = false;
    // Archive member holding the bytes, empty when the region is described but
    // not captured (memory-mapped I/O has no side-effect-free contents).
    QString contentsMember;
    // Populated during capture and while a snapshot is loaded; not serialized
    // inline, it travels as the archive member named by contentsMember.
    QByteArray contents;
};

struct CpuSnapshot {
    QString architecture;                        // "m68k:68030", "ppc:601"
    std::uint32_t pc = 0;
    QStringList registerLines;                   // debugRegisterLines(), verbatim
    QMap<QString, std::uint64_t> registers;      // d0..d7, a0..a7, sr, vbr, usp, isp, msp
    QMap<QString, std::uint64_t> mmuRegisters;   // tc, crp/srp, tt0/tt1, mmusr, fault address
    QStringList disassembly;                     // rendered window around pc
    QStringList backtrace;                       // frame-pointer chain walk, best effort
    QVector<std::uint32_t> vectorTable;          // longs read from VBR
    std::uint32_t stackPointer = 0;
    std::uint32_t framePointer = 0;
    int interruptLevel = 0;
    bool halted = false;
    bool stopped = false;
};

struct DeviceSnapshot {
    QString id;                                     // "via1", "nubus-slot-9"
    QString kind;                                   // "via6522", "ncr5380"
    QStringList stateLines;                         // human-readable
    QMap<QString, QString> fields;                  // machine-readable
    QVector<QPair<QString, QByteArray>> blobs;      // pram, card registers
};

struct MachineSnapshot {
    int schemaVersion = machineSnapshotSchemaVersion;
    QString machineId;
    std::uint64_t cycle = 0;
    bool overlayEnabled = false;
    bool romLoaded = false;
    CpuSnapshot cpu;
    QVector<MemoryRegion> memory;
    QVector<DeviceSnapshot> devices;
    devices::video::VideoFrame frame;   // already carries CLUT, depth, stride, masks
    QStringList schedulerEvents;
    QMap<QString, QStringList> traces;  // ring name -> rendered records
    QStringList notes;                  // capture warnings and degraded reasons

    [[nodiscard]] bool valid() const { return !machineId.isEmpty(); }
};

// Shared shape for video devices. Carries the CLUT alongside VRAM, because
// indexed VRAM cannot be decoded without it, and records the mode metadata a
// reader needs to interpret the raw bytes.
inline DeviceSnapshot makeVideoSnapshot(const QString& id, const QString& kind,
    const devices::video::VideoFrame& frame, const QByteArray& vram)
{
    using devices::video::BitOrder;
    using devices::video::ByteOrder;
    using devices::video::PixelStorage;

    DeviceSnapshot snapshot;
    snapshot.id = id;
    snapshot.kind = kind;
    snapshot.fields.insert(QStringLiteral("width"), QString::number(frame.width));
    snapshot.fields.insert(QStringLiteral("height"), QString::number(frame.height));
    snapshot.fields.insert(QStringLiteral("stride_bytes"), QString::number(frame.strideBytes));
    snapshot.fields.insert(QStringLiteral("bits_per_pixel"), QString::number(frame.bitsPerPixel));
    snapshot.fields.insert(QStringLiteral("storage"),
        frame.storage == PixelStorage::Indexed ? QStringLiteral("indexed") : QStringLiteral("direct"));
    snapshot.fields.insert(QStringLiteral("byte_order"),
        frame.byteOrder == ByteOrder::BigEndian ? QStringLiteral("big") : QStringLiteral("little"));
    snapshot.fields.insert(QStringLiteral("bit_order"),
        frame.bitOrder == BitOrder::MostSignificantFirst ? QStringLiteral("msb") : QStringLiteral("lsb"));
    snapshot.fields.insert(QStringLiteral("grabbable"),
        frame.grabbable ? QStringLiteral("yes") : QStringLiteral("no"));
    snapshot.fields.insert(QStringLiteral("clut_entries"), QString::number(frame.colorTable.size()));
    snapshot.fields.insert(QStringLiteral("red_mask"),
        QStringLiteral("0x%1").arg(frame.channels.redMask, 8, 16, QLatin1Char('0')));
    snapshot.fields.insert(QStringLiteral("green_mask"),
        QStringLiteral("0x%1").arg(frame.channels.greenMask, 8, 16, QLatin1Char('0')));
    snapshot.fields.insert(QStringLiteral("blue_mask"),
        QStringLiteral("0x%1").arg(frame.channels.blueMask, 8, 16, QLatin1Char('0')));

    if (!vram.isEmpty()) snapshot.blobs.append({ QStringLiteral("vram"), vram });
    if (!frame.colorTable.isEmpty()) {
        QByteArray clut;
        clut.reserve(frame.colorTable.size() * 4);
        for (const auto entry : frame.colorTable) {
            clut.append(static_cast<char>((entry >> 24) & 0xff));
            clut.append(static_cast<char>((entry >> 16) & 0xff));
            clut.append(static_cast<char>((entry >> 8) & 0xff));
            clut.append(static_cast<char>(entry & 0xff));
        }
        snapshot.blobs.append({ QStringLiteral("clut"), clut });
    }
    return snapshot;
}

} // namespace cutemac::debug
