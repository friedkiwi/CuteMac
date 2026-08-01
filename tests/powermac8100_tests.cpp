#include <cstdlib>
#include <iostream>

#include <QCoreApplication>
#include <QFile>
#include <QTemporaryDir>

#include "cutemac/machines/powermac8100/PowerMac8100Machine.h"

namespace {
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
    require(machine.debugRead8(0x50f2c000U) == 0xffU, "diagnostic pin inactive");
    machine.debugWrite32(0x100, 0x12345678U);
    require(machine.debugRead32(0x100) == 0x12345678U, "RAM mapping");
    std::cout << "Power Macintosh 8100 machine tests passed\n";
    return 0;
}
