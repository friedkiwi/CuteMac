#include <algorithm>
#include <cstdint>
#include <iostream>

#include <QCoreApplication>
#include <QFile>

namespace {
std::uint32_t readBe32(const char* bytes)
{
    return (static_cast<std::uint32_t>(static_cast<std::uint8_t>(bytes[0])) << 24)
        | (static_cast<std::uint32_t>(static_cast<std::uint8_t>(bytes[1])) << 16)
        | (static_cast<std::uint32_t>(static_cast<std::uint8_t>(bytes[2])) << 8)
        | static_cast<std::uint8_t>(bytes[3]);
}
void writeBe32(char* bytes, std::uint32_t value)
{
    bytes[0] = static_cast<char>(value >> 24); bytes[1] = static_cast<char>(value >> 16);
    bytes[2] = static_cast<char>(value >> 8); bytes[3] = static_cast<char>(value);
}
QString field(const QByteArray& entry, int offset)
{
    const auto bytes = entry.mid(offset, 32); return QString::fromLatin1(bytes.constData(), bytes.indexOf('\0'));
}
}

int main(int argc, char** argv)
{
    QCoreApplication application(argc, argv);
    if (argc != 4) {
        std::cerr << "usage: CuteMacWrapHfsScsi <driver-template> <raw-hfs-volume> <output>\n";
        return 2;
    }
    QFile templateFile(QString::fromLocal8Bit(argv[1])), volumeFile(QString::fromLocal8Bit(argv[2]));
    if (!templateFile.open(QIODevice::ReadOnly) || !volumeFile.open(QIODevice::ReadOnly)) return 1;
    auto disk = templateFile.readAll(); const auto volume = volumeFile.readAll();
    if (disk.size() < 1024 || volume.isEmpty() || (volume.size() % 512) != 0
        || disk[0] != 'E' || disk[1] != 'R') return 1;
    const auto partitionCount = readBe32(disk.constData() + 512 + 4);
    qsizetype hfsEntry = -1, freeEntry = -1; std::uint32_t hfsStart = 0;
    for (std::uint32_t index = 1; index <= partitionCount; ++index) {
        const auto offset = static_cast<qsizetype>(index) * 512;
        if (offset + 512 > disk.size() || disk[offset] != 'P' || disk[offset + 1] != 'M') return 1;
        const auto entry = disk.mid(offset, 512); const auto type = field(entry, 48);
        if (type == QStringLiteral("Apple_HFS")) { hfsEntry = offset; hfsStart = readBe32(entry.constData() + 8); }
        else if (type == QStringLiteral("Apple_Free")) freeEntry = offset;
    }
    const auto volumeBlocks = static_cast<std::uint32_t>(volume.size() / 512);
    const auto totalBlocks = static_cast<std::uint32_t>(disk.size() / 512);
    if (hfsEntry < 0 || hfsStart + volumeBlocks > totalBlocks) return 1;
    std::copy(volume.begin(), volume.end(), disk.begin() + static_cast<qsizetype>(hfsStart) * 512);
    writeBe32(disk.data() + hfsEntry + 12, volumeBlocks);
    writeBe32(disk.data() + hfsEntry + 84, volumeBlocks);
    if (freeEntry >= 0) {
        const auto freeStart = hfsStart + volumeBlocks;
        writeBe32(disk.data() + freeEntry + 8, freeStart);
        writeBe32(disk.data() + freeEntry + 12, totalBlocks - freeStart);
        writeBe32(disk.data() + freeEntry + 84, totalBlocks - freeStart);
    }
    QFile output(QString::fromLocal8Bit(argv[3]));
    if (!output.open(QIODevice::WriteOnly | QIODevice::Truncate) || output.write(disk) != disk.size()) return 1;
    return 0;
}
