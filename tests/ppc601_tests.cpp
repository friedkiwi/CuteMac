#include "cutemac/cpu/ppc/PpcCpuCore.h"

#include <array>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <map>
#include <limits>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace {

using Core = cutemac::cpu::ppc::PowerPc601Core;

class TestBus final : public cutemac::cpu::ppc::PowerPcBus {
public:
    static constexpr std::size_t size = 2 * 1024 * 1024;
    std::array<std::uint8_t, size> bytes {};

    std::uint8_t read8(std::uint32_t address) override { return bytes[address % size]; }
    std::uint16_t read16(std::uint32_t address) override
    {
        return static_cast<std::uint16_t>((read8(address) << 8) | read8(address + 1));
    }
    std::uint32_t read32(std::uint32_t address) override
    {
        return (static_cast<std::uint32_t>(read8(address)) << 24)
            | (static_cast<std::uint32_t>(read8(address + 1)) << 16)
            | (static_cast<std::uint32_t>(read8(address + 2)) << 8) | read8(address + 3);
    }
    void write8(std::uint32_t address, std::uint8_t value) override { bytes[address % size] = value; }
    void write16(std::uint32_t address, std::uint16_t value) override
    {
        write8(address, static_cast<std::uint8_t>(value >> 8)); write8(address + 1, static_cast<std::uint8_t>(value));
    }
    void write32(std::uint32_t address, std::uint32_t value) override
    {
        write8(address, static_cast<std::uint8_t>(value >> 24)); write8(address + 1, static_cast<std::uint8_t>(value >> 16));
        write8(address + 2, static_cast<std::uint8_t>(value >> 8)); write8(address + 3, static_cast<std::uint8_t>(value));
    }
};

void require(bool condition, const char* message)
{
    if (!condition) { std::cerr << "FAIL: " << message << '\n'; std::exit(1); }
}

std::uint32_t d(unsigned primary, unsigned rd, unsigned ra, std::uint16_t immediate)
{
    return (primary << 26) | (rd << 21) | (ra << 16) | immediate;
}
std::uint32_t x(unsigned rd, unsigned ra, unsigned rb, unsigned xo, bool rc = false, bool oe = false)
{
    return (31U << 26) | (rd << 21) | (ra << 16) | (rb << 11) | (oe ? 0x400U : 0U) | (xo << 1) | rc;
}

struct Fixture {
    TestBus bus;
    Core cpu;
    Fixture()
    {
        cpu.setBus(&bus); cpu.reset(); cpu.setProgramCounter(0x100);
        auto state = cpu.registers(); state.msr = Core::msrMe; cpu.setRegisters(state);
    }
    void instruction(std::uint32_t address, std::uint32_t opcode) { bus.write32(address, opcode); }
};

void testResetAndInteger()
{
    Fixture f;
    require(Core().id() == QStringLiteral("cpu.ppc601"), "core id");
    Core resetCore; resetCore.reset();
    require(resetCore.programCounter() == 0xfff00100U, "601 reset vector");
    require(resetCore.registers().msr == (Core::msrMe | Core::msrIp), "601 reset MSR");

    f.instruction(0x100, d(14, 3, 0, 0x7fff));       // li r3,32767
    f.instruction(0x104, d(15, 4, 0, 0x1234));       // lis r4,0x1234
    f.instruction(0x108, x(5, 3, 4, 266, true));     // add. r5,r3,r4
    f.instruction(0x10c, d(28, 5, 6, 0xff00));       // andi. r6,r5,0xff00
    for (int i = 0; i < 4; ++i) (void)f.cpu.stepInstruction();
    const auto s = f.cpu.registers();
    require(s.gpr[3] == 0x7fff && s.gpr[4] == 0x12340000, "immediate arithmetic");
    require(s.gpr[5] == 0x12347fff && s.gpr[6] == 0x7f00, "add and logical");
    require((s.cr >> 28) == 4, "record-form CR0");
}

void testMemoryBranchAndReservation()
{
    Fixture f;
    auto s = f.cpu.registers(); s.gpr[3] = 0x400; s.gpr[4] = 0x11223344; f.cpu.setRegisters(s);
    f.instruction(0x100, d(36, 4, 3, 0));            // stw r4,0(r3)
    f.instruction(0x104, d(32, 5, 3, 0));            // lwz r5,0(r3)
    f.instruction(0x108, x(6, 3, 0, 20));            // lwarx r6,r3,r0
    f.instruction(0x10c, x(6, 3, 0, 150, true));     // stwcx. r6,r3,r0
    f.instruction(0x110, (18U << 26) | 8U);          // b +8
    f.instruction(0x114, d(14, 7, 0, 1));
    f.instruction(0x118, d(14, 7, 0, 2));
    for (int i = 0; i < 6; ++i) (void)f.cpu.stepInstruction();
    s = f.cpu.registers();
    require(f.bus.read32(0x400) == 0x11223344 && s.gpr[5] == 0x11223344, "load/store");
    require((s.cr & 0x20000000U) != 0, "successful reservation store");
    require(s.gpr[7] == 2, "unconditional branch");
}

void testExceptionsAndInterrupt()
{
    Fixture f;
    f.instruction(0x100, 0); // illegal
    (void)f.cpu.stepInstruction();
    auto s = f.cpu.registers();
    require(s.pc == 0x700 && s.srr0 == 0x100 && (s.srr1 & 0x00080000U), "illegal instruction exception");

    f.cpu.setProgramCounter(0x200); s = f.cpu.registers(); s.msr = Core::msrMe | Core::msrEe; f.cpu.setRegisters(s);
    f.cpu.setExternalInterrupt(true); (void)f.cpu.stepInstruction(); s = f.cpu.registers();
    require(s.pc == 0x500 && s.srr0 == 0x200, "external interrupt exception");
}

void testBatAndPageTranslation()
{
    Fixture f;
    auto s = f.cpu.registers();
    s.msr = Core::msrMe | Core::msrDr | Core::msrIr;
    s.batu[0] = 0x00000002U; // BEPI 0, supervisor read/write
    s.batl[0] = 0x00100040U; // RPN 1 MiB, valid, 128 KiB
    f.cpu.setRegisters(s);
    require(f.cpu.translateForDebug(0x1234, Core::AccessType::Read) == 0x101234U, "601 unified BAT translation");

    s = f.cpu.registers(); s.batl[0] = 0; s.sdr1 = 0x00010000; s.sr[0] = 0x00000001; f.cpu.setRegisters(s);
    const std::uint32_t ea = 0x00002034; const std::uint32_t pageIndex = 2; const std::uint32_t hash = 1 ^ pageIndex;
    const std::uint32_t pteg = 0x00010000 | (hash << 6);
    f.bus.write32(pteg, 0x80000000U | (1U << 7) | (pageIndex >> 10));
    f.bus.write32(pteg + 4, 0x00080002U);
    require(f.cpu.translateForDebug(ea, Core::AccessType::Read) == 0x00080034U, "hashed page translation");
    require((f.bus.read32(pteg + 4) & 0x180U) == 0, "debug translation has no R/C side effects");
}

void testTraceAndDisassembly()
{
    Fixture f; std::vector<Core::TraceEvent> trace;
    f.cpu.setTraceSink([&](const auto& event) { trace.push_back(event); });
    f.instruction(0x100, d(14, 3, 0, 42));
    (void)f.cpu.stepInstruction();
    require(!trace.empty(), "trace events captured");
    require(trace.back().kind == Core::TraceEvent::Kind::Instruction && trace.back().opcode == d(14, 3, 0, 42), "instruction trace payload");
    require(f.cpu.disassembleOpcode(0x100, d(14, 3, 0, 42)).startsWith(QStringLiteral("li r3,42")), "PowerPC disassembly");
    require(Core::formatTraceEvent(trace.back()).startsWith(QStringLiteral("v=1 arch=ppc601 kind=insn")), "stable trace format");
}

void testArithmeticVectorMatrix()
{
    constexpr std::array<std::uint32_t, 9> values {
        0, 1, 2, 0x7fffffffU, 0x80000000U, 0xfffffffeU, 0xffffffffU, 0x55555555U, 0xaaaaaaaaU
    };
    for (const auto a : values) for (const auto b : values) {
        Fixture f; auto s = f.cpu.registers(); s.gpr[3] = a; s.gpr[4] = b; f.cpu.setRegisters(s);
        f.instruction(0x100, x(5, 3, 4, 266, true, true));
        (void)f.cpu.stepInstruction(); s = f.cpu.registers();
        const auto result = a + b;
        const bool overflow = ((~(a ^ b) & (a ^ result)) & 0x80000000U) != 0;
        require(s.gpr[5] == result, "addo vector result");
        require(((s.xer & 0x40000000U) != 0) == overflow, "addo vector overflow");

        Fixture g; s = g.cpu.registers(); s.gpr[3] = a; s.gpr[4] = b; g.cpu.setRegisters(s);
        g.instruction(0x100, x(5, 3, 4, 10, false));
        (void)g.cpu.stepInstruction(); s = g.cpu.registers();
        require(s.gpr[5] == result, "addc vector result");
        require(((s.xer & 0x20000000U) != 0) == (static_cast<std::uint64_t>(a) + b > 0xffffffffULL), "addc vector carry");
    }
}

void test601SpecificAndTimebase()
{
    Fixture f; auto s = f.cpu.registers();
    s.gpr[3] = 31; s.gpr[4] = 0; s.gpr[6] = 0; f.cpu.setRegisters(s);
    f.instruction(0x100, x(6, 4, 3, 29, true));      // maskg r4,r6,r3 (POWER operand aliases)
    f.instruction(0x104, d(9, 7, 3, 4));             // dozi r7,r3,4
    (void)f.cpu.stepInstruction(); (void)f.cpu.stepInstruction();
    s = f.cpu.registers();
    require(s.gpr[4] == 0xffffffffU, "601 maskg wraparound mask");
    require(s.gpr[7] == 0, "601 dozi signed difference-or-zero");

    s.dec = 0x00000100U; s.rtcl = 0; f.cpu.setRegisters(s); f.cpu.setClockFrequency(80'000'000);
    f.cpu.advanceTime(11); s = f.cpu.registers();
    require(s.dec == 0x00000080U && s.rtcl == 128U, "601 7.8125 MHz DEC/RTC tick");
    f.cpu.advanceTime(10); s = f.cpu.registers();
    require(s.dec == 0 && s.rtcl == 256U, "601 DEC low seven bits unimplemented");
}

void testPrivilegedStateAndPreciseFaults()
{
    Fixture f;
    auto s = f.cpu.registers();
    s.gpr[3] = Core::msrMe | Core::msrFp;
    s.gpr[4] = 0x12345678U;
    s.gpr[5] = 0xa5a5a5a5U;
    s.gpr[6] = 0x30000000U;
    f.cpu.setRegisters(s);
    f.instruction(0x100, x(3, 0, 0, 146));          // mtmsr r3
    f.instruction(0x104, x(4, 7, 0, 210));          // mtsr 7,r4
    f.instruction(0x108, x(5, 0, 6, 242));          // mtsrin r5,r6
    for (int i = 0; i < 3; ++i) (void)f.cpu.stepInstruction();
    s = f.cpu.registers();
    require(s.msr == (Core::msrMe | Core::msrFp), "mtmsr state");
    require(s.sr[7] == 0x12345678U && s.sr[3] == 0xa5a5a5a5U, "segment register writes");

    Fixture fault;
    s = fault.cpu.registers(); s.gpr[3] = 0x101; fault.cpu.setRegisters(s);
    fault.instruction(0x100, x(4, 3, 0, 20));       // misaligned lwarx
    (void)fault.cpu.stepInstruction(); s = fault.cpu.registers();
    require(s.pc == 0x600 && s.srr0 == 0x100 && s.dar == 0x101, "precise alignment exception");

    Fixture unaligned;
    s = unaligned.cpu.registers(); s.gpr[3] = 0x401; s.gpr[4] = 0x11223344U; unaligned.cpu.setRegisters(s);
    unaligned.instruction(0x100, d(36, 4, 3, 0));
    unaligned.instruction(0x104, d(32, 5, 3, 0));
    (void)unaligned.cpu.stepInstruction(); (void)unaligned.cpu.stepInstruction();
    require(unaligned.cpu.registers().gpr[5] == 0x11223344U, "big-endian unaligned integer access");
}

std::uint64_t doubleBits(double value)
{
    std::uint64_t bits; std::memcpy(&bits, &value, sizeof(bits)); return bits;
}

void testFloatingPoint()
{
    Fixture f; auto s = f.cpu.registers(); s.msr |= Core::msrFp; s.fpr[1] = doubleBits(1.5); s.fpr[2] = doubleBits(2.25); f.cpu.setRegisters(s);
    f.instruction(0x100, (63U << 26) | (3U << 21) | (1U << 16) | (2U << 11) | (21U << 1));
    (void)f.cpu.stepInstruction(); s = f.cpu.registers();
    require(s.fpr[3] == doubleBits(3.75), "deterministic fadd result");
}

int runExternalIntegerVectors(const char* path)
{
    std::ifstream input(path);
    if (!input) { std::cerr << "unable to open vector file: " << path << '\n'; return 2; }
    std::string line; int tested = 0, failed = 0; std::map<std::string, int> failuresByMnemonic;
    while (std::getline(input, line)) {
        if (line.empty() || line[0] == '#') continue;
        std::vector<std::string> fields; std::stringstream stream(line); std::string field;
        while (std::getline(stream, field, ',')) fields.push_back(field);
        if (fields.size() < 2) continue;
        const auto opcode = static_cast<std::uint32_t>(std::stoul(fields[1], nullptr, 0));
        std::uint32_t inputA = 0, inputB = 0, expectedDest = 0, expectedXer = 0, expectedCr = 0;
        bool hasDest = false;
        for (std::size_t i = 2; i < fields.size(); ++i) {
            const auto separator = fields[i].find('='); if (separator == std::string::npos) continue;
            const auto name = fields[i].substr(0, separator); const auto value = static_cast<std::uint32_t>(std::stoul(fields[i].substr(separator + 1), nullptr, 0));
            if (name == "rA") inputA = value; else if (name == "rB") inputB = value;
            else if (name == "rD") { expectedDest = value; hasDest = true; }
            else if (name == "XER") expectedXer = value; else if (name == "CR") expectedCr = value;
        }
        Fixture fixture; auto state = fixture.cpu.registers(); state.gpr[3] = inputA; state.gpr[4] = inputB; fixture.cpu.setRegisters(state);
        fixture.instruction(0x100, opcode); (void)fixture.cpu.stepInstruction(); state = fixture.cpu.registers(); ++tested;
        if ((hasDest && state.gpr[3] != expectedDest) || state.xer != expectedXer || state.cr != expectedCr) {
            if (failed < 30) std::cerr << "vector mismatch " << fields[0] << " opcode=0x" << std::hex << opcode
                << " got rD=0x" << state.gpr[3] << " XER=0x" << state.xer << " CR=0x" << state.cr
                << " expected rD=0x" << expectedDest << " XER=0x" << expectedXer << " CR=0x" << expectedCr << std::dec << '\n';
            ++failed;
            ++failuresByMnemonic[fields[0]];
        }
    }
    std::cout << "external integer vectors: " << tested << " tested, " << failed << " failed\n";
    for (const auto& [mnemonic, count] : failuresByMnemonic) std::cout << "  " << mnemonic << ": " << count << '\n';
    return failed == 0 ? 0 : 1;
}

double parseFloatingValue(const std::string& text)
{
    if (text == "DBL_MAX") return std::numeric_limits<double>::max();
    if (text == "-DBL_MAX") return -std::numeric_limits<double>::max();
    if (text == "DBL_MIN") return std::numeric_limits<double>::min();
    if (text == "-DBL_MIN") return -std::numeric_limits<double>::min();
    if (text == "FLT_MAX") return std::numeric_limits<float>::max();
    if (text == "-FLT_MAX") return -std::numeric_limits<float>::max();
    if (text == "FLT_MIN") return std::numeric_limits<float>::min();
    if (text == "-FLT_MIN") return -std::numeric_limits<float>::min();
    if (text == "qnan") return std::numeric_limits<double>::quiet_NaN();
    if (text == "snan") return std::numeric_limits<double>::signaling_NaN();
    return std::stod(text);
}

int runExternalFloatingVectors(const char* path)
{
    std::ifstream input(path);
    if (!input) { std::cerr << "unable to open vector file: " << path << '\n'; return 2; }
    std::string line; int tested = 0, failed = 0; std::map<std::string, int> failuresByMnemonic;
    while (std::getline(input, line)) {
        if (line.empty() || line[0] == '#') continue;
        std::vector<std::string> fields; std::stringstream stream(line); std::string field;
        while (std::getline(stream, field, ',')) fields.push_back(field);
        if (fields.size() < 2) continue;
        const auto opcode = static_cast<std::uint32_t>(std::stoul(fields[1], nullptr, 0));
        std::uint64_t expectedDest = 0; std::uint32_t expectedFpscr = 0, expectedCr = 0, initialFpscr = 0;
        double inputA = 0, inputB = 0, inputC = 0; bool hasDest = false;
        for (std::size_t i = 2; i < fields.size(); ++i) {
            const auto separator = fields[i].find('='); if (separator == std::string::npos) continue;
            const auto name = fields[i].substr(0, separator), value = fields[i].substr(separator + 1);
            if (name == "frA") inputA = parseFloatingValue(value); else if (name == "frB") inputB = parseFloatingValue(value);
            else if (name == "frC") inputC = parseFloatingValue(value);
            else if (name == "frD") { expectedDest = std::stoull(value, nullptr, 0); hasDest = true; }
            else if (name == "FPSCR") expectedFpscr = static_cast<std::uint32_t>(std::stoul(value, nullptr, 0));
            else if (name == "CR") expectedCr = static_cast<std::uint32_t>(std::stoul(value, nullptr, 0));
            else if (name == "round") initialFpscr = value == "RTZ" ? 1U : value == "RPI" ? 2U : value == "RNI" ? 3U : value == "VEN" ? 0x80U : 0U;
        }
        Fixture fixture; auto state = fixture.cpu.registers(); state.msr |= Core::msrFp; state.fpscr = initialFpscr;
        state.fpr[(opcode >> 16) & 31U] = doubleBits(inputA); state.fpr[(opcode >> 11) & 31U] = doubleBits(inputB); state.fpr[(opcode >> 6) & 31U] = doubleBits(inputC); fixture.cpu.setRegisters(state);
        fixture.instruction(0x100, opcode); (void)fixture.cpu.stepInstruction(); state = fixture.cpu.registers(); ++tested;
        const auto dest = state.fpr[(opcode >> 21) & 31U];
        if ((hasDest && dest != expectedDest) || state.fpscr != expectedFpscr || state.cr != expectedCr) {
            if (failed < 20) std::cerr << "FP vector mismatch " << fields[0] << " opcode=0x" << std::hex << opcode
                << " got frD=0x" << dest << " FPSCR=0x" << state.fpscr << " CR=0x" << state.cr
                << " expected frD=0x" << expectedDest << " FPSCR=0x" << expectedFpscr << " CR=0x" << expectedCr << std::dec << '\n';
            ++failed; ++failuresByMnemonic[fields[0]];
        }
    }
    std::cout << "external floating vectors: " << tested << " tested, " << failed << " failed\n";
    for (const auto& [mnemonic, count] : failuresByMnemonic) std::cout << "  " << mnemonic << ": " << count << '\n';
    return failed == 0 ? 0 : 1;
}

} // namespace

int main(int argc, char** argv)
{
    if (argc == 3 && std::string(argv[1]) == "--integer-vectors") return runExternalIntegerVectors(argv[2]);
    if (argc == 3 && std::string(argv[1]) == "--floating-vectors") return runExternalFloatingVectors(argv[2]);
    testResetAndInteger();
    testMemoryBranchAndReservation();
    testExceptionsAndInterrupt();
    testBatAndPageTranslation();
    testTraceAndDisassembly();
    testArithmeticVectorMatrix();
    test601SpecificAndTimebase();
    testPrivilegedStateAndPreciseFaults();
    testFloatingPoint();
    std::cout << "PowerPC 601 core tests passed\n";
    return 0;
}
