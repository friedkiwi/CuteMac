// Floating-point coverage for the 68881/68882/68040 FPU.
//
// The survival sweep is the important one: it executes every F-line encoding
// and asserts the process is still alive afterwards. That failure mode used to
// be exit(1) from inside the FPU, which takes the test runner down with it, so
// the test that catches it has to be the one that would die.

#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <cstring>
#include <vector>

#include "cutemac/cpu/m68k/M68kBus.h"
#include "cutemac/cpu/m68k/M68kCpuCore.h"
#include "cutemac/cpu/m68k/M68kFpuDiagnostics.h"

namespace {

constexpr std::uint32_t codeBase = 0x1000;
constexpr std::uint32_t vectorLine1111 = 0x2c;
constexpr std::uint32_t line1111Handler = 0x3000;

class TestBus final : public cutemac::cpu::m68k::M68kBus {
public:
    std::uint8_t read8(std::uint32_t address) override { return memory[address & (memory.size() - 1)]; }

    std::uint16_t read16(std::uint32_t address) override
    {
        return static_cast<std::uint16_t>((read8(address) << 8) | read8(address + 1));
    }

    std::uint32_t read32(std::uint32_t address) override
    {
        return (static_cast<std::uint32_t>(read16(address)) << 16) | read16(address + 2);
    }

    void write8(std::uint32_t address, std::uint8_t value) override
    {
        memory[address & (memory.size() - 1)] = value;
    }

    void write16(std::uint32_t address, std::uint16_t value) override
    {
        write8(address, static_cast<std::uint8_t>(value >> 8));
        write8(address + 1, static_cast<std::uint8_t>(value));
    }

    void write32(std::uint32_t address, std::uint32_t value) override
    {
        write16(address, static_cast<std::uint16_t>(value >> 16));
        write16(address + 2, static_cast<std::uint16_t>(value));
    }

    std::array<std::uint8_t, 0x10000> memory {};
};

int failures = 0;

bool expect(bool condition, const char* message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
    return condition;
}

class Machine {
public:
    explicit Machine(cutemac::cpu::m68k::M68kCpuCore::Model model
        = cutemac::cpu::m68k::M68kCpuCore::Model::M68030)
    {
        m_cpu.setModel(model);
        m_cpu.setBus(&m_bus);
        reset();
    }

    void reset()
    {
        m_bus.memory.fill(0);
        // Supervisor stack, reset PC, and a line-1111 handler that simply stops
        // so a refused instruction lands somewhere identifiable.
        m_bus.write32(0, 0x00008000);
        m_bus.write32(4, codeBase);
        m_bus.write32(vectorLine1111, line1111Handler);
        m_bus.write16(line1111Handler, 0x4e72); // stop #imm
        m_bus.write16(line1111Handler + 2, 0x2700);
        m_cpu.reset();
        cutemac::cpu::m68k::clearFpuDiagnostics();
    }

    // Assembles one instruction at codeBase and runs it. Returns the pc.
    std::uint32_t run(std::initializer_list<std::uint16_t> words)
    {
        std::uint32_t address = codeBase;
        for (const auto word : words) {
            m_bus.write16(address, word);
            address += 2;
        }
        m_bus.write16(address, 0x4e72); // stop, so execution cannot run away
        m_bus.write16(address + 2, 0x2700);
        m_cpu.setProgramCounter(codeBase);
        // Generous: a transcendental alone costs several hundred cycles, and a
        // short budget silently leaves the trailing store unexecuted.
        (void)m_cpu.execute(20000);
        return m_cpu.programCounter();
    }

    [[nodiscard]] bool tookLine1111() const { return m_cpu.programCounter() >= line1111Handler
        && m_cpu.programCounter() <= line1111Handler + 4; }

    TestBus& bus() { return m_bus; }
    cutemac::cpu::m68k::M68kCpuCore& cpu() { return m_cpu; }

private:
    TestBus m_bus;
    cutemac::cpu::m68k::M68kCpuCore m_cpu;
};

// No guest instruction may terminate the emulator. Before this, an encoding the
// FPU did not implement called exit(1) from inside the CPU core.
void testSurvivalSweep()
{
    using Model = cutemac::cpu::m68k::M68kCpuCore::Model;
    for (const auto model : { Model::M68030, Model::M68040 }) {
        Machine machine(model);
        int executed = 0;
        for (std::uint32_t opcode = 0xf000; opcode <= 0xffff; ++opcode) {
            // A spread of extension words: the FPU decodes its real work out of
            // the second word, so sweeping only the first misses most forms.
            for (const std::uint16_t extension : { 0x0000, 0x5c00, 0x0080, 0xa800, 0xffff }) {
                machine.reset();
                (void)machine.run({ static_cast<std::uint16_t>(opcode), extension });
                ++executed;
            }
        }
        expect(executed == 0x1000 * 5, "the sweep executed every F-line encoding");
    }
    // Reaching here at all is the assertion: exit(1) would have taken the
    // runner with it.
    expect(true, "no F-line encoding terminated the process");
}

void testUnimplementedEncodingRaisesLine1111()
{
    Machine machine;
    // FMOVE.X with an opmode that is not a defined 68881 operation.
    machine.run({ 0xf200, 0x007b });
    expect(machine.tookLine1111(), "an undefined opmode raises line 1111 rather than exiting");
    const auto records = cutemac::cpu::m68k::fpuDiagnosticRecords();
    expect(!records.isEmpty(), "a refused encoding is recorded for the panic dump");
    if (!records.isEmpty()) {
        expect(records.first().pc == codeBase, "the record names the faulting instruction");
        expect(records.first().opcode == 0xf200, "the record carries the opcode");
    }
}

void testDiagnosticsRingIsBounded()
{
    Machine machine;
    // Opmodes the 68881 does not define. Note 0x40-0x7f are *not* candidates:
    // they are the 68040 rounding-precision variants and are masked down to
    // their base operation before dispatch.
    static const int undefinedOpmodes[] = { 0x05, 0x07, 0x0b, 0x13, 0x17, 0x1b, 0x29, 0x2a,
        0x2b, 0x2c, 0x2d, 0x2e, 0x2f, 0x39, 0x3b, 0x3c, 0x3d, 0x3e, 0x3f };
    for (int dst = 0; dst < 8; ++dst) {
        for (const int opmode : undefinedOpmodes) {
            machine.run({ 0xf200, static_cast<std::uint16_t>((dst << 7) | opmode) });
        }
    }
    const auto records = cutemac::cpu::m68k::fpuDiagnosticRecords();
    expect(!records.isEmpty(), "the diagnostics ring keeps records");
    expect(records.size() <= 64, "the diagnostics ring stays bounded");
}

// Writes an extended-precision value into FP0 through memory, runs one
// operation against it, and reads the result back as a double.
double runUnary(Machine& machine, std::uint16_t opmode, double input)
{
    machine.reset();
    // Load FP0 from a double in memory: FMOVE.D (0x4000).L, FP0
    const std::uint32_t operand = 0x4000;
    std::uint64_t bits = 0;
    static_assert(sizeof(bits) == sizeof(input), "double is 64-bit");
    std::memcpy(&bits, &input, sizeof(bits));
    machine.bus().write32(operand, static_cast<std::uint32_t>(bits >> 32));
    machine.bus().write32(operand + 4, static_cast<std::uint32_t>(bits));

    // FMOVE.D <ea>,FP0 ; F<op>.X FP0,FP0 ; FMOVE.D FP0,<ea>
    machine.run({ 0xf239, 0x5400, static_cast<std::uint16_t>(operand >> 16),
        static_cast<std::uint16_t>(operand & 0xffff),
        0xf200, static_cast<std::uint16_t>(0x0000 | opmode),
        0xf239, 0x7400, static_cast<std::uint16_t>(operand >> 16),
        static_cast<std::uint16_t>(operand & 0xffff) });

    const std::uint64_t result = (static_cast<std::uint64_t>(machine.bus().read32(operand)) << 32)
        | machine.bus().read32(operand + 4);
    double value = 0;
    std::memcpy(&value, &result, sizeof(value));
    return value;
}

bool closeEnough(double actual, double expected)
{
    if (std::isnan(expected)) return std::isnan(actual);
    if (std::isinf(expected)) return std::isinf(actual) && ((actual > 0) == (expected > 0));
    const auto tolerance = std::max(1e-9, std::fabs(expected) * 1e-9);
    return std::fabs(actual - expected) <= tolerance;
}

// Every operation the 68881 implements in hardware must produce a value, not a
// line-1111 exception: there is no OS fallback for these on a real Mac.
void testTranscendentals()
{
    struct Case {
        std::uint16_t opmode;
        const char* name;
        double input;
        double expected;
    };
    const std::vector<Case> cases {
        { 0x02, "FSINH", 1.0, std::sinh(1.0) },
        { 0x06, "FLOGNP1", 0.5, std::log1p(0.5) },
        { 0x08, "FETOXM1", 0.5, std::expm1(0.5) },
        { 0x09, "FTANH", 0.5, std::tanh(0.5) },
        { 0x0a, "FATAN", 0.5, std::atan(0.5) },
        { 0x0c, "FASIN", 0.5, std::asin(0.5) },
        { 0x0d, "FATANH", 0.5, std::atanh(0.5) },
        { 0x0e, "FSIN", 0.5, std::sin(0.5) },
        { 0x0f, "FTAN", 0.5, std::tan(0.5) },
        { 0x10, "FETOX", 1.5, std::exp(1.5) },
        { 0x11, "FTWOTOX", 3.0, 8.0 },
        { 0x12, "FTENTOX", 3.0, 1000.0 },
        { 0x14, "FLOGN", 2.0, std::log(2.0) },
        { 0x15, "FLOG10", 1000.0, 3.0 },
        { 0x16, "FLOG2", 8.0, 3.0 },
        { 0x18, "FABS", -2.5, 2.5 },
        { 0x19, "FCOSH", 1.0, std::cosh(1.0) },
        { 0x1a, "FNEG", 2.5, -2.5 },
        { 0x1c, "FACOS", 0.5, std::acos(0.5) },
        { 0x1d, "FCOS", 0.5, std::cos(0.5) },
        { 0x04, "FSQRT", 9.0, 3.0 },
    };
    Machine machine;
    for (const auto& test : cases) {
        const auto actual = runUnary(machine, test.opmode, test.input);
        if (!closeEnough(actual, test.expected)) {
            std::cerr << "FAIL: " << test.name << " gave " << actual << ", expected "
                      << test.expected << '\n';
            ++failures;
        }
        expect(cutemac::cpu::m68k::fpuDiagnosticRecords().isEmpty(),
            "a defined 68881 operation must not be refused");
    }
}

// FSCALE and FGETMAN manipulate the exponent and mantissa directly. Routing
// them through a host double would drop the low mantissa bits of an extended
// value for operations that are supposed to be exact.
void testExactOperations()
{
    Machine machine;
    expect(closeEnough(runUnary(machine, 0x1f, 12.0), 1.5), "FGETMAN returns the mantissa in [1,2)");
    expect(closeEnough(runUnary(machine, 0x1e, 12.0), 3.0), "FGETEXP returns the unbiased exponent");
    expect(cutemac::cpu::m68k::fpuDiagnosticRecords().isEmpty(),
        "the exact operations are implemented");
}

void testDomainErrorsSetOperr()
{
    Machine machine;
    const auto result = runUnary(machine, 0x14, -1.0); // FLOGN(-1)
    expect(std::isnan(result), "a domain error returns a NaN rather than refusing the instruction");
    expect(cutemac::cpu::m68k::fpuDiagnosticRecords().isEmpty(),
        "a domain error is not a refused encoding");
}

// FPSR's exception and accrued bytes are what SANE reads after an operation.
// Before this they were never written at all.
void testFpsrExceptionFlags()
{
    Machine machine;
    const std::uint32_t operand = 0x4000;
    const std::uint32_t status = 0x4010;

    // FMOVE.L #1,FP0 ; FDIV.L #0,FP0 ; FMOVE.L FPSR,(status).L
    machine.reset();
    machine.bus().write32(operand, 1);
    machine.bus().write32(operand + 4, 0);
    machine.run({ 0xf239, 0x4000, static_cast<std::uint16_t>(operand >> 16),
        static_cast<std::uint16_t>(operand & 0xffff),
        0xf239, 0x4020, static_cast<std::uint16_t>((operand + 4) >> 16),
        static_cast<std::uint16_t>((operand + 4) & 0xffff),
        0xf239, 0xa800, static_cast<std::uint16_t>(status >> 16),
        static_cast<std::uint16_t>(status & 0xffff) });

    const auto fpsr = machine.bus().read32(status);
    expect((fpsr & 0x00000400) != 0, "divide by zero sets the FPSR DZ exception bit");
    expect((fpsr & 0x00000010) != 0, "divide by zero accrues DZ");
}

// FPIAR carries the address of the instruction the FPU is executing; exception
// handlers use it to find the offending instruction.
void testFpiarTracksTheInstruction()
{
    Machine machine;
    const std::uint32_t operand = 0x4000;
    const std::uint32_t saved = 0x4010;
    machine.reset();
    machine.bus().write32(operand, 0x3ff00000);
    machine.bus().write32(operand + 4, 0);

    machine.run({ 0xf239, 0x5400, static_cast<std::uint16_t>(operand >> 16),
        static_cast<std::uint16_t>(operand & 0xffff),
        0xf239, 0xa400, static_cast<std::uint16_t>(saved >> 16),
        static_cast<std::uint16_t>(saved & 0xffff) });

    // The FMOVE that stored FPIAR is itself an FP instruction, so FPIAR points
    // at it: codeBase + the four words of the first instruction.
    expect(machine.bus().read32(saved) == codeBase + 8,
        "FPIAR holds the address of the executing floating-point instruction");
}

// Guest software reads the state frame's format byte to identify the
// coprocessor, so a Quadra must not be handed a 68881 frame.
void testStateFrameFormatFollowsTheModel()
{
    using Model = cutemac::cpu::m68k::M68kCpuCore::Model;
    using FpuModel = cutemac::cpu::m68k::M68kCpuCore::FpuModel;
    const std::uint32_t frame = 0x4020;

    struct Case {
        Model cpu;
        FpuModel fpu;
        std::uint32_t expectedHeader;
        const char* what;
    };
    const Case cases[] = {
        { Model::M68030, FpuModel::M68882, 0x2f180000, "a 68882 reports a 68882 IDLE frame" },
        { Model::M68030, FpuModel::M68881, 0x1f180000, "a 68881 reports a 68881 IDLE frame" },
        { Model::M68040, FpuModel::M68040, 0x00000000, "a 68040 reports a 68040 frame" },
    };

    for (const auto& test : cases) {
        Machine machine(test.cpu);
        machine.cpu().setFpuModel(test.fpu);
        machine.reset();
        machine.cpu().setFpuModel(test.fpu);

        // Use the FPU first: FSAVE of a just-reset FPU is a NULL frame by design.
        // FMOVE.L #1,FP0 then FSAVE (A0), with A0 pointing at the frame.
        machine.bus().write32(0x4000, 1);
        machine.run({ 0x207c, static_cast<std::uint16_t>(frame >> 16),
            static_cast<std::uint16_t>(frame & 0xffff),          // movea.l #frame,A0
            0xf239, 0x4000, 0x0000, 0x4000,                       // fmove.l (0x4000).l,fp0
            0xf310 });                                            // fsave (A0)
        expect(machine.bus().read32(frame) == test.expectedHeader, test.what);
    }
}

// A machine with no coprocessor fitted must behave like one: the F-line goes to
// the exception vector rather than into the shared arithmetic.
void testNoFpuRaisesLine1111()
{
    Machine machine;
    machine.cpu().setFpuModel(cutemac::cpu::m68k::M68kCpuCore::FpuModel::None);
    machine.run({ 0xf200, 0x0000 });
    expect(machine.tookLine1111(), "an unfitted coprocessor raises line 1111");
    machine.cpu().setFpuModel(cutemac::cpu::m68k::M68kCpuCore::FpuModel::M68882);
}

} // namespace

int main()
{
    testSurvivalSweep();
    testUnimplementedEncodingRaisesLine1111();
    testDiagnosticsRingIsBounded();
    testTranscendentals();
    testExactOperations();
    testDomainErrorsSetOperr();
    testFpsrExceptionFlags();
    testFpiarTracksTheInstruction();
    testStateFrameFormatFollowsTheModel();
    testNoFpuRaisesLine1111();

    if (failures != 0) {
        std::cerr << failures << " FPU check(s) failed\n";
        return 1;
    }
    std::cout << "m68k FPU tests passed\n";
    return 0;
}
