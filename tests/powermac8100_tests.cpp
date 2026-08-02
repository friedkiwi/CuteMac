#include <cstdlib>
#include <iostream>
#include <memory>

#include <QCoreApplication>
#include <QFile>
#include <QTemporaryDir>

#include "cutemac/machines/powermac8100/PowerMac8100Machine.h"
#include "cutemac/devices/cuda/CudaController.h"
#include "cutemac/devices/scsi/ncr53c94/Ncr53c94.h"
#include "cutemac/devices/video/SonoraVideo.h"

namespace {
class TestTarget final : public cutemac::devices::scsi::ScsiTarget {
public:
    bool ready() const override { return true; }
    cutemac::devices::scsi::ScsiCommandResult executeCommand(const QByteArray&, const QByteArray&) override
    { return { QByteArray::fromHex("01020304"), 0, 0, 0 }; }
};

void require(bool condition, const char* message)
{
    if (!condition) { std::cerr << "FAIL: " << message << '\n'; std::exit(1); }
}
}

int main(int argc, char** argv)
{
    QCoreApplication application(argc, argv);
    QTemporaryDir directory;
    require(directory.isValid(), "temporary directory");
    QByteArray rom(4 * 1024 * 1024, 0);
    // Reset alias + 0x300100 maps to ROM offset 0x300100.
    rom[0x300100] = 0x38; rom[0x300101] = 0x60; rom[0x300102] = 0x00; rom[0x300103] = 0x2a; // li r3,42
    const auto path = directory.filePath(QStringLiteral("8100.rom"));
    QFile file(path); require(file.open(QIODevice::WriteOnly), "open test ROM");
    require(file.write(rom) == rom.size(), "write test ROM"); file.close();

    cutemac::machines::powermac8100::PowerMac8100Machine machine(8 * 1024 * 1024);
    require(machine.loadRomFile(path, {}), "load 4 MiB ROM");
    machine.reset();
    require(machine.programCounter() == 0xfff00100U, "601 reset address");
    (void)machine.stepInstruction();
    require(machine.cpuRegisters().gpr[3] == 42, "execute instruction through reset ROM alias");
    require(machine.debugRead32(0x40000000U + 0x300100U) == 0x3860002aU, "normal ROM mapping");
    require(machine.debugRead32(0x5ffffffcU) == 0x00003013U, "8100 machine ID");
    require(machine.debugRead8(0x5ffffffcU) == 0xa5U && machine.debugRead16(0x5ffffffcU) == 0xa55aU,
        "partial machine ID reads retain signature");
    require(machine.debugRead8(0x50f2c000U) == 1U && machine.debugRead8(0x50f2c001U) == 0U,
        "factory-test pin and diagnostic aperture");
    machine.debugWrite8(0x50f26002U, 0x40U);
    require((machine.debugRead8(0x50f26002U) & 0x40U) != 0, "Sonora VBL acknowledge");
    (void)machine.runCycles(1'330'100);
    require((machine.debugRead8(0x50f26002U) & 0x40U) == 0,
        "Sonora VBL continues while video output is blanked");
    require(machine.compatibilityRead(0, 4, 0x68000000U) == 0U,
        "68k compatibility reset view starts in ROM");
    require(!machine.compatibilityRead(0xffffffffU, 1, 0x68000000U).has_value()
        && !machine.compatibilityWrite(0xffffffffU, 0, 1, 0x68000000U),
        "68k compatibility RAM bounds reject wrapped addresses");
    require(machine.compatibilityWrite(0x60b00000U, 0x89abcdefU, 4, 0x68000000U)
        && machine.compatibilityRead(0x60b00000U, 4, 0x68000000U) == 0x89abcdefU,
        "68k compatibility onboard-video aperture is coherent");
    require(machine.compatibilityRead(0x60c00000U, 4, 0x68000000U) == 0xffffffffU,
        "68k compatibility NuBus probes see open bus outside onboard video");
    require(!machine.compatibilityRead(0x6802e100U, 4, 0x68000000U).has_value()
        && !machine.compatibilityWrite(0x68ffffacU, 0, 4, 0x68000000U),
        "nanokernel logical segment still follows PPC translation");
    require(machine.compatibilityWrite(0x100, 0x89abcdefU, 4, 0x68000000U)
        && machine.compatibilityRead(0x100, 4, 0x68000000U) == 0x89abcdefU,
        "68k compatibility writes switch the low-memory view to RAM");
    machine.debugWrite32(0x100, 0x12345678U);
    require(machine.debugRead32(0x100) == 0x12345678U, "RAM mapping");

    const auto diskPath = directory.filePath(QStringLiteral("internal-scsi.img"));
    QByteArray diskBytes(512, 0);
    diskBytes.replace(0, 4, QByteArray::fromHex("01020304"));
    QFile diskFile(diskPath); require(diskFile.open(QIODevice::WriteOnly), "open internal SCSI image");
    require(diskFile.write(diskBytes) == diskBytes.size(), "write internal SCSI image"); diskFile.close();
    require(machine.loadScsiDisk(0, diskPath, false), "attach internal SCSI disk");
    for (const auto byte : QByteArray::fromHex("80080000000100"))
        machine.debugWrite8(0x50f11020U, static_cast<std::uint8_t>(byte));
    machine.debugWrite8(0x50f11040U, 0); machine.debugWrite8(0x50f11030U, 0x42);
    (void)machine.debugRead8(0x50f11050U);
    machine.debugWrite8(0x50f11000U, 0); machine.debugWrite8(0x50f11010U, 2);
    machine.debugWrite8(0x50f11030U, 0x90);
    machine.debugWrite32(0x50f32000U, 0x00001000U);
    machine.debugWrite8(0x50f32008U, 0x02);
    require(machine.debugRead32(0x1000U) == 0x01020304U,
        "AMIC DMA routes internal SCSI data into RAM");

    const auto cdPath = directory.filePath(QStringLiteral("installer.iso"));
    QByteArray cdBytes(2 * 2048, 0);
    cdBytes.replace(0, 8, QByteArrayLiteral("CDIMAGE!"));
    QFile cdFile(cdPath); require(cdFile.open(QIODevice::WriteOnly), "open internal SCSI CD image");
    require(cdFile.write(cdBytes) == cdBytes.size(), "write internal SCSI CD image"); cdFile.close();
    require(!machine.loadScsiCdRom(7, cdPath), "reject invalid internal SCSI CD ID");
    require(machine.loadScsiCdRom(3, cdPath), "attach internal SCSI CD-ROM");
    machine.reset();
    for (const auto byte : QByteArray::fromHex("80120000002400"))
        machine.debugWrite8(0x50f11020U, static_cast<std::uint8_t>(byte));
    machine.debugWrite8(0x50f11040U, 3); machine.debugWrite8(0x50f11030U, 0x42);
    (void)machine.debugRead8(0x50f11050U);
    machine.debugWrite8(0x50f11000U, 36); machine.debugWrite8(0x50f11010U, 0);
    machine.debugWrite8(0x50f11030U, 0x90);
    machine.debugWrite32(0x50f32000U, 0x00002000U);
    machine.debugWrite8(0x50f32008U, 0x02);
    require(machine.debugRead32(0x2000U) == 0x05800201U,
        "internal SCSI CD-ROM survives reset and responds to INQUIRY through AMIC DMA");
    machine.ejectScsiCdRom(3);
    require(machine.loadScsiCdRom(3, {}), "empty internal SCSI CD-ROM remains selectable");

    cutemac::machines::powermac8100::PowerMac8100Machine expanded(16 * 1024 * 1024);
    require(expanded.loadRomFile(path, {}), "load expanded-memory test ROM"); expanded.reset();
    const std::uint64_t hmcConfig = std::uint64_t { 2 } << 29; // 8 MiB SIMM decode spacing
    for (unsigned bit = 0; bit < 35; ++bit)
        expanded.write8(0x50f40000U, static_cast<std::uint8_t>((hmcConfig >> bit) & 1U));
    expanded.debugWrite32(0x00800000U, 0x89abcdefU);
    require(expanded.debugRead32(0x10000000U) == 0x89abcdefU, "bank A sizing alias");
    expanded.debugWrite32(0x01000000U, 0x76543210U);
    require(expanded.debugRead32(0x0fc00000U) == 0x76543210U, "small bank B warm-start alias");

    cutemac::devices::video::SonoraVideo video;
    video.reset(); video.writeControl(0, 0x06); video.writeControl(1, 0);
    QVector<std::uint8_t> vram(1024 * 1024, 0xff);
    const auto frame = video.frame(vram);
    require(frame.valid() && frame.width == 640 && frame.height == 480 && frame.bitsPerPixel == 1,
        "PDM Sonora framebuffer contract");
    require(frame.colorTable.size() == 2 && frame.colorTable[0] == 0xffffffffU
        && frame.colorTable[1] == 0xff7f7f7fU, "PDM one-bit Ariel palette wiring");

    cutemac::devices::scsi::ncr53c94::Ncr53c94 scsi;
    scsi.reset(); scsi.attachTarget(0, std::make_shared<TestTarget>());
    for (const auto byte : QByteArray::fromHex("80080000000400")) scsi.writeRegister(2, static_cast<std::uint8_t>(byte));
    scsi.writeRegister(4, 0); scsi.writeRegister(3, 0x42);
    require(scsi.interruptActive() && (scsi.readRegister(5) & 0x18U), "53C94 selection and service interrupt");
    scsi.writeRegister(0, 4); scsi.writeRegister(1, 0); scsi.writeRegister(3, 0x90);
    require(scsi.readDmaWord() == 0x0201U && scsi.readDmaWord() == 0x0403U,
        "53C94 pseudo-DMA data transfer");
    scsi.writeRegister(3, 0x01); scsi.writeRegister(4, 0); scsi.writeRegister(3, 0xc1);
    require(scsi.interruptActive() && (scsi.readRegister(4) & 7U) == 2U,
        "53C94 DMA selection can precede command-byte transfer");

    cutemac::devices::via6522::Via6522 via;
    cutemac::devices::cuda::CudaController cuda;
    via.setPowerOnState(0, 0, 0, 0); cuda.attach(&via);
    via.setPortBChangedCallback([&](std::uint8_t output, std::uint8_t direction) { cuda.portBChanged(output, direction); });
    via.setShiftRegisterWriteCallback([&](std::uint8_t value) { cuda.shiftByteFromHost(value); });
    via.reset(); cuda.reset();
    via.writeRegister(2, 0x30); via.writeRegister(0, 0x30); via.writeRegister(11, 0x1c);
    via.writeRegister(0, 0x10); // assert TIP
    via.writeRegister(0, 0x30); // release synchronous attention
    cuda.tick(4'880);
    require((via.readRegister(13) & 0x04U) != 0, "Cuda completes synchronous attention with an SR interrupt");
    (void)via.readRegister(10);
    via.writeRegister(10, 1); via.writeRegister(0, 0x10); // TIP low latches packet type
    via.writeRegister(10, 3); via.writeRegister(0, 0x00); // BYTEACK edge latches command
    via.writeRegister(0, 0x30); // finish request; Cuda asserts TREQ
    cuda.tick(1'040);
    require((via.readRegister(0) & 0x08U) == 0, "Cuda response asserts TREQ");
    via.writeRegister(11, 0x0c);
    (void)via.readRegister(10); // transaction-end dummy byte
    via.writeRegister(0, 0x10); // TIP low starts response
    cuda.tick(7'040);
    require(via.readRegister(10) == 1, "Cuda response packet type");
    via.writeRegister(0, 0x00); cuda.tick(7'040); require(via.readRegister(10) == 0, "Cuda response status");
    via.writeRegister(0, 0x10); cuda.tick(7'040); require(via.readRegister(10) == 3, "Cuda response echoes command");
    std::cout << "Power Macintosh 8100 machine tests passed\n";
    return 0;
}
