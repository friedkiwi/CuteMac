// Offline equivalent of CuteMacDebugSession's "floppy status"/"floppy scan"
// commands, for hosts where the debug session is unavailable: it needs
// libreadline, which the Windows build has no source for.
//
// Decodes every generated track back into sectors and verifies each one
// against the source image, so a report of "reads clean" means the emulated
// read path reproduces the file rather than merely producing plausible
// framing.

#include <cstdint>
#include <iostream>

#include <QByteArray>
#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>

#include "cutemac/devices/floppy/FloppyDiskImage.h"

namespace {

constexpr int bytesPerSector = 512;

std::uint16_t mfmCrc(const QByteArray& bytes)
{
    std::uint16_t crc = 0xffff;
    for (const auto byte : bytes) {
        crc = static_cast<std::uint16_t>(crc ^ (static_cast<std::uint8_t>(byte) << 8));
        for (int bit = 0; bit < 8; ++bit) {
            crc = static_cast<std::uint16_t>((crc & 0x8000) != 0 ? (crc << 1) ^ 0x1021 : crc << 1);
        }
    }
    return crc;
}

struct TrackReport {
    int addressMarks = 0;
    int dataMarks = 0;
    int sectorsVerified = 0;
    int crcFailures = 0;
    int payloadMismatches = 0;
};

// Walks a generated track the way the guest does: find an 0xA1A1A1 0xFE
// address field, then the following 0xA1A1A1 0xFB data field, and check both
// the CRC and the payload against the image.
TrackReport scanTrack(const QByteArray& track, const QByteArray& image, int physicalTrack, int side)
{
    TrackReport report;
    for (qsizetype i = 0; i + 3 < track.size(); ++i) {
        const auto isSync = static_cast<std::uint8_t>(track[i]) == 0xa1
            && static_cast<std::uint8_t>(track[i + 1]) == 0xa1
            && static_cast<std::uint8_t>(track[i + 2]) == 0xa1;
        if (!isSync) continue;
        const auto type = static_cast<std::uint8_t>(track[i + 3]);
        if (type == 0xfe) {
            ++report.addressMarks;
            continue;
        }
        if (type != 0xfb) continue;
        ++report.dataMarks;

        const auto payloadStart = i + 4;
        if (payloadStart + bytesPerSector + 2 > track.size()) continue;
        const auto payload = track.mid(payloadStart, bytesPerSector);

        QByteArray crcBytes;
        crcBytes.append(track.mid(i, 4));
        crcBytes.append(payload);
        const auto expected = mfmCrc(crcBytes);
        const auto stored = static_cast<std::uint16_t>(
            (static_cast<std::uint8_t>(track[payloadStart + bytesPerSector]) << 8)
            | static_cast<std::uint8_t>(track[payloadStart + bytesPerSector + 1]));
        if (expected != stored) ++report.crcFailures;

        // Sector n of this track, in the order buildMfmTrack emits them.
        const auto sectorIndex = report.dataMarks - 1;
        const auto block = (physicalTrack * 2 + side) * 18 + sectorIndex;
        const auto offset = static_cast<qsizetype>(block) * bytesPerSector;
        if (offset + bytesPerSector <= image.size()) {
            if (image.mid(offset, bytesPerSector) != payload) ++report.payloadMismatches;
            else ++report.sectorsVerified;
        }
        i = payloadStart + bytesPerSector + 1;
    }
    return report;
}

} // namespace

int main(int argc, char** argv)
{
    QCoreApplication application(argc, argv);
    if (argc < 2) {
        std::cerr << "usage: CuteMacFloppyProbe <image> [more images...]\n";
        return 2;
    }

    int failures = 0;
    for (int argument = 1; argument < argc; ++argument) {
        const auto path = QString::fromLocal8Bit(argv[argument]);
        std::cout << "=== " << QFileInfo(path).fileName().toStdString() << " ===\n";

        QFile file(path);
        if (!file.open(QIODevice::ReadOnly)) {
            std::cout << "  cannot open\n";
            ++failures;
            continue;
        }
        const auto image = file.readAll();

        cutemac::devices::floppy::FloppyDiskImage floppy;
        if (!floppy.load(path, true)) {
            std::cout << "  load REFUSED (size=" << image.size() << ")\n";
            ++failures;
            continue;
        }

        const auto state = floppy.debugState();
        std::cout << "  format=" << state.imageFormat.toStdString()
                  << " blocks=" << floppy.blockCount()
                  << " double_sided=" << (floppy.doubleSided() ? "yes" : "no")
                  << " high_density=" << (floppy.highDensity() ? "yes" : "no") << '\n';

        TrackReport total;
        int emptyTracks = 0;
        for (int track = 0; track < floppy.trackCount(); ++track) {
            for (int side = 0; side < (floppy.doubleSided() ? 2 : 1); ++side) {
                const auto bytes = floppy.trackBytesForDebug(track, side);
                if (bytes.isEmpty()) {
                    ++emptyTracks;
                    continue;
                }
                const auto report = scanTrack(bytes, image, track, side);
                total.addressMarks += report.addressMarks;
                total.dataMarks += report.dataMarks;
                total.sectorsVerified += report.sectorsVerified;
                total.crcFailures += report.crcFailures;
                total.payloadMismatches += report.payloadMismatches;
                if (track == 0 && side == 0) {
                    std::cout << "  track0/side0: bytes=" << bytes.size()
                              << " address_marks=" << report.addressMarks
                              << " data_marks=" << report.dataMarks
                              << " verified=" << report.sectorsVerified
                              << " crc_fail=" << report.crcFailures
                              << " payload_mismatch=" << report.payloadMismatches << '\n';
                }
            }
        }

        std::cout << "  all tracks: address_marks=" << total.addressMarks
                  << " data_marks=" << total.dataMarks
                  << " verified=" << total.sectorsVerified
                  << " crc_fail=" << total.crcFailures
                  << " payload_mismatch=" << total.payloadMismatches
                  << " empty_tracks=" << emptyTracks << '\n';

        if (total.crcFailures != 0 || total.payloadMismatches != 0 || emptyTracks != 0) ++failures;
    }
    return failures == 0 ? 0 : 1;
}
