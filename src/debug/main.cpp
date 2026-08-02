#include <readline/history.h>
#include <readline/readline.h>

#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QMap>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStringList>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTextStream>

#include <algorithm>
#include <array>
#include <cstring>
#include <cstdlib>
#include <memory>
#include <optional>
#include <set>

#include "cutemac/config/Configuration.h"
#include "cutemac/core/EmulationSession.h"
#include "cutemac/core/IDebugCpuAccess.h"
#include "cutemac/debug/SadMacDetector.h"
#include "cutemac/machines/maciicx/MacIIcxMachine.h"
#include "cutemac/machines/macplus/MacPlusMachine.h"
#include "cutemac/machines/powermac8100/PowerMac8100Machine.h"
#include "cutemac/devices/video/nubus/MacintoshIIVideoCard.h"
#include "cutemac/session/FramebufferRenderer.h"

namespace {

constexpr std::uint32_t macPlusScreenWidth = 512;
constexpr std::uint32_t macPlusScreenHeight = 342;
constexpr qsizetype maxDebugTraceEntries = 8192;

QStringList g_completionWords;

char* completionGenerator(const char* text, int state)
{
    static QStringList matches;
    static qsizetype index = 0;
    if (state == 0) {
        matches.clear();
        index = 0;
        const auto prefix = QString::fromLocal8Bit(text).toLower();
        for (const auto& word : g_completionWords) {
            if (word.startsWith(prefix)) {
                matches.append(word);
            }
        }
    }
    if (index >= matches.size()) {
        return nullptr;
    }
    return ::strdup(matches[index++].toLocal8Bit().constData());
}

char** commandCompletion(const char* text, int start, int)
{
    rl_attempted_completion_over = 1;
    if (start == 0) {
        return rl_completion_matches(text, completionGenerator);
    }
    return rl_completion_matches(text, completionGenerator);
}

std::optional<std::uint32_t> parseNumber(const QString& text)
{
    bool ok = false;
    auto value = text;
    if (value.startsWith(QStringLiteral("0x"), Qt::CaseInsensitive)) {
        return value.mid(2).toUInt(&ok, 16);
    }
    if (value.startsWith(QLatin1Char('$'))) {
        return value.mid(1).toUInt(&ok, 16);
    }

    const auto parsed = value.toUInt(&ok, 10);
    return ok ? std::optional<std::uint32_t>(parsed) : std::nullopt;
}

QString hexValue(std::uint32_t value, int width = 8)
{
    return QStringLiteral("0x%1").arg(value, width, 16, QLatin1Char('0'));
}

QString byteToHex(std::uint8_t value)
{
    return QStringLiteral("%1").arg(value, 2, 16, QLatin1Char('0'));
}

QString bytesToHex(const QByteArray& bytes)
{
    QString result;
    result.reserve(bytes.size() * 2);
    for (const auto byte : bytes) {
        result += byteToHex(static_cast<std::uint8_t>(byte));
    }
    return result;
}

int countPattern(const QByteArray& bytes, const QByteArray& pattern)
{
    if (bytes.isEmpty() || pattern.isEmpty() || pattern.size() > bytes.size()) {
        return 0;
    }

    int count = 0;
    for (qsizetype i = 0; i <= bytes.size() - pattern.size(); ++i) {
        if (bytes.mid(i, pattern.size()) == pattern) {
            ++count;
        }
    }
    return count;
}

QByteArray hexToBytes(const QString& hex)
{
    QByteArray bytes;
    for (int i = 0; i + 1 < hex.size(); i += 2) {
        bool ok = false;
        const auto value = hex.mid(i, 2).toUInt(&ok, 16);
        if (!ok) {
            return {};
        }
        bytes.append(static_cast<char>(value));
    }
    return bytes;
}

std::uint32_t readBe32FromBytes(const QByteArray& bytes, qsizetype offset)
{
    if (offset + 4 > bytes.size()) {
        return 0;
    }
    return (static_cast<std::uint32_t>(static_cast<std::uint8_t>(bytes[offset])) << 24)
        | (static_cast<std::uint32_t>(static_cast<std::uint8_t>(bytes[offset + 1])) << 16)
        | (static_cast<std::uint32_t>(static_cast<std::uint8_t>(bytes[offset + 2])) << 8)
        | static_cast<std::uint8_t>(bytes[offset + 3]);
}

void appendBe32(QByteArray& bytes, std::uint32_t value)
{
    bytes.append(static_cast<char>(value >> 24));
    bytes.append(static_cast<char>(value >> 16));
    bytes.append(static_cast<char>(value >> 8));
    bytes.append(static_cast<char>(value));
}

void appendLe16(QByteArray& bytes, std::uint16_t value)
{
    bytes.append(static_cast<char>(value));
    bytes.append(static_cast<char>(value >> 8));
}

void appendLe32(QByteArray& bytes, std::uint32_t value)
{
    bytes.append(static_cast<char>(value));
    bytes.append(static_cast<char>(value >> 8));
    bytes.append(static_cast<char>(value >> 16));
    bytes.append(static_cast<char>(value >> 24));
}

class GdbStub {
public:
    GdbStub(cutemac::machines::macplus::MacPlusMachine& machine, std::set<std::uint32_t>& breakpoints)
        : m_machine(machine)
        , m_breakpoints(breakpoints)
    {
    }

    void setPort(quint16 port) { m_port = port; }
    [[nodiscard]] quint16 port() const { return m_port; }
    [[nodiscard]] bool enabled() const { return m_enabled; }
    void setEnabled(bool enabled) { m_enabled = enabled; }

    void serve(QTextStream& out)
    {
        if (!m_enabled) {
            out << "gdb stub is disabled\n";
            return;
        }

        QTcpServer server;
        if (!server.listen(QHostAddress::LocalHost, m_port)) {
            out << "failed to listen on localhost:" << m_port << ": " << server.errorString() << '\n';
            return;
        }

        out << "gdb stub listening on localhost:" << server.serverPort() << '\n';
        out << "gdb-multiarch: set architecture m68k:68000; target remote localhost:" << server.serverPort() << '\n';
        out.flush();

        if (!server.waitForNewConnection(-1)) {
            out << "gdb accept failed\n";
            return;
        }

        auto* socket = server.nextPendingConnection();
        while (socket->state() == QAbstractSocket::ConnectedState) {
            const auto packet = readPacket(*socket);
            if (!packet.has_value()) {
                break;
            }
            if (!handlePacket(*socket, *packet)) {
                break;
            }
        }
        socket->disconnectFromHost();
        socket->deleteLater();
        out << "gdb client disconnected\n";
    }

private:
    std::optional<QString> readPacket(QTcpSocket& socket)
    {
        QByteArray data;
        bool inPacket = false;
        while (socket.waitForReadyRead(-1)) {
            const auto chunk = socket.readAll();
            for (const auto ch : chunk) {
                if (!inPacket) {
                    if (ch == '$') {
                        inPacket = true;
                        data.clear();
                    }
                    continue;
                }
                if (ch == '#') {
                    while (socket.bytesAvailable() < 2) {
                        if (!socket.waitForReadyRead(-1)) {
                            return std::nullopt;
                        }
                    }
                    (void)socket.read(2);
                    socket.write("+");
                    socket.flush();
                    return QString::fromLatin1(data);
                }
                data.append(ch);
            }
        }
        return std::nullopt;
    }

    void sendPacket(QTcpSocket& socket, const QString& payload)
    {
        std::uint8_t checksum = 0;
        const auto data = payload.toLatin1();
        for (const auto ch : data) {
            checksum = static_cast<std::uint8_t>(checksum + static_cast<std::uint8_t>(ch));
        }

        const auto packet = QByteArray("$") + data + "#" + byteToHex(checksum).toLatin1();
        socket.write(packet);
        socket.flush();
    }

    bool handlePacket(QTcpSocket& socket, const QString& packet)
    {
        if (packet == QStringLiteral("?")) {
            sendPacket(socket, QStringLiteral("S05"));
        } else if (packet.startsWith(QStringLiteral("qSupported"))) {
            sendPacket(socket, QStringLiteral("PacketSize=4000"));
        } else if (packet == QStringLiteral("qfThreadInfo")) {
            sendPacket(socket, QStringLiteral("m1"));
        } else if (packet == QStringLiteral("qsThreadInfo")) {
            sendPacket(socket, QStringLiteral("l"));
        } else if (packet.startsWith(QLatin1Char('T'))) {
            sendPacket(socket, QStringLiteral("OK"));
        } else if (packet.startsWith(QStringLiteral("qSymbol"))) {
            sendPacket(socket, QStringLiteral("OK"));
        } else if (packet.startsWith(QStringLiteral("vMustReplyEmpty"))) {
            sendPacket(socket, QString());
        } else if (packet == QStringLiteral("qAttached")) {
            sendPacket(socket, QStringLiteral("1"));
        } else if (packet == QStringLiteral("qC")) {
            sendPacket(socket, QStringLiteral("QC1"));
        } else if (packet == QStringLiteral("qOffsets")) {
            sendPacket(socket, QStringLiteral("Text=0;Data=0;Bss=0"));
        } else if (packet.startsWith(QLatin1Char('H'))) {
            sendPacket(socket, QStringLiteral("OK"));
        } else if (packet == QStringLiteral("g")) {
            sendPacket(socket, registerPacket());
        } else if (packet.startsWith(QLatin1Char('m'))) {
            sendPacket(socket, readMemoryPacket(packet.mid(1)));
        } else if (packet.startsWith(QLatin1Char('M'))) {
            sendPacket(socket, writeMemoryPacket(packet.mid(1)));
        } else if (packet.startsWith(QStringLiteral("Z0,"))) {
            sendPacket(socket, addBreakpoint(packet.mid(3)) ? QStringLiteral("OK") : QStringLiteral("E01"));
        } else if (packet.startsWith(QStringLiteral("z0,"))) {
            sendPacket(socket, removeBreakpoint(packet.mid(3)) ? QStringLiteral("OK") : QStringLiteral("E01"));
        } else if (packet.startsWith(QLatin1Char('s'))) {
            maybeSetPc(packet.mid(1));
            (void)m_machine.stepInstruction();
            sendPacket(socket, QStringLiteral("S05"));
        } else if (packet.startsWith(QLatin1Char('c'))) {
            maybeSetPc(packet.mid(1));
            continueForGdb();
            sendPacket(socket, QStringLiteral("S05"));
        } else if (packet == QStringLiteral("D") || packet == QStringLiteral("k")) {
            sendPacket(socket, QStringLiteral("OK"));
            return false;
        } else {
            sendPacket(socket, QString());
        }
        return true;
    }

    QString registerPacket() const
    {
        const auto regs = m_machine.cpuRegisters();
        QByteArray bytes;
        for (const auto value : regs.d) {
            appendBe32(bytes, value);
        }
        for (const auto value : regs.a) {
            appendBe32(bytes, value);
        }
        appendBe32(bytes, regs.sr);
        appendBe32(bytes, regs.pc);
        return bytesToHex(bytes);
    }

    QString readMemoryPacket(const QString& argument) const
    {
        const auto parts = argument.split(QLatin1Char(','));
        if (parts.size() != 2) {
            return QStringLiteral("E01");
        }

        bool okAddress = false;
        bool okLength = false;
        const auto address = parts[0].toUInt(&okAddress, 16);
        const auto length = parts[1].toUInt(&okLength, 16);
        if (!okAddress || !okLength || length > 4096) {
            return QStringLiteral("E02");
        }

        QByteArray bytes;
        bytes.reserve(static_cast<int>(length));
        for (std::uint32_t i = 0; i < length; ++i) {
            bytes.append(static_cast<char>(m_machine.debugRead8(address + i)));
        }
        return bytesToHex(bytes);
    }

    QString writeMemoryPacket(const QString& argument)
    {
        const auto separator = argument.indexOf(QLatin1Char(':'));
        if (separator < 0) {
            return QStringLiteral("E01");
        }
        const auto header = argument.left(separator).split(QLatin1Char(','));
        if (header.size() != 2) {
            return QStringLiteral("E01");
        }

        bool okAddress = false;
        bool okLength = false;
        const auto address = header[0].toUInt(&okAddress, 16);
        const auto length = header[1].toUInt(&okLength, 16);
        const auto bytes = hexToBytes(argument.mid(separator + 1));
        if (!okAddress || !okLength || bytes.size() != static_cast<int>(length)) {
            return QStringLiteral("E02");
        }

        for (int i = 0; i < bytes.size(); ++i) {
            m_machine.debugWrite8(address + static_cast<std::uint32_t>(i), static_cast<std::uint8_t>(bytes[i]));
        }
        return QStringLiteral("OK");
    }

    bool addBreakpoint(const QString& argument)
    {
        const auto parts = argument.split(QLatin1Char(','));
        if (parts.isEmpty()) {
            return false;
        }
        bool ok = false;
        const auto address = parts[0].toUInt(&ok, 16);
        if (!ok) {
            return false;
        }
        m_breakpoints.insert(address & 0x00ffffff);
        return true;
    }

    bool removeBreakpoint(const QString& argument)
    {
        const auto parts = argument.split(QLatin1Char(','));
        if (parts.isEmpty()) {
            return false;
        }
        bool ok = false;
        const auto address = parts[0].toUInt(&ok, 16);
        if (!ok) {
            return false;
        }
        m_breakpoints.erase(address & 0x00ffffff);
        return true;
    }

    void maybeSetPc(const QString& addressText)
    {
        if (addressText.isEmpty()) {
            return;
        }
        bool ok = false;
        const auto address = addressText.toUInt(&ok, 16);
        if (ok) {
            // The current core wrapper only exposes PC mutation indirectly to
            // the emulator. GDB PC writes will be wired when full register
            // writes are added.
            (void)address;
        }
    }

    void continueForGdb()
    {
        constexpr int maxInstructions = 200000;
        for (int i = 0; i < maxInstructions; ++i) {
            if (m_breakpoints.count(m_machine.programCounter()) != 0) {
                return;
            }
            (void)m_machine.stepInstruction();
        }
    }

    cutemac::machines::macplus::MacPlusMachine& m_machine;
    std::set<std::uint32_t>& m_breakpoints;
    bool m_enabled = false;
    quint16 m_port = 1234;
};

class DebugConsole {
public:
    explicit DebugConsole(cutemac::config::Configuration configuration)
        : m_configuration(std::move(configuration))
    {
        reloadMachine();
    }

    int run()
    {
        installCompletion();
        printBanner();
        while (true) {
            const std::unique_ptr<char, decltype(&std::free)> input(readline("cutemac-debug> "), &std::free);
            if (!input) {
                m_out << '\n';
                return 0;
            }

            const auto line = QString::fromLocal8Bit(input.get()).trimmed();
            if (line.isEmpty()) {
                continue;
            }
            add_history(input.get());

            if (!executeCommand(line)) {
                return 0;
            }
        }
    }

private:
    struct TraceOptions {
        bool pc = false;
        bool irq = false;
        bool trap = false;
        bool driver = false;
        bool lowmem = false;
        bool screen = false;
        bool sound = false;
        bool iwm = false;
        bool floppy = false;
        bool timeline = false;
    };

    struct MemorySnapshot {
        std::uint32_t address = 0;
        QByteArray bytes;
    };

    struct SadMacCapture {
        bool armed = false;
        bool detected = false;
        std::uint64_t cycle = 0;
        std::uint32_t pc = 0;
        QString reason;
        std::optional<std::uint32_t> primaryCode;
        std::optional<std::uint32_t> secondaryCode;
        QStringList registers;
        QStringList instructions;
        cutemac::devices::video::VideoFrame frame;
    };

    static QStringList commandWords()
    {
        return {
            QStringLiteral("regs"), QStringLiteral("state"), QStringLiteral("devices"),
            QStringLiteral("step"), QStringLiteral("run"), QStringLiteral("run-until"), QStringLiteral("run-until-event"),
            QStringLiteral("disasm"), QStringLiteral("mem"), QStringLiteral("mem-find"),
            QStringLiteral("mem-snapshot"), QStringLiteral("memory-diff"),
            QStringLiteral("write8"), QStringLiteral("write16"), QStringLiteral("write32"),
            QStringLiteral("break"), QStringLiteral("delete"), QStringLiteral("breaks"),
            QStringLiteral("watch"), QStringLiteral("lowmem"), QStringLiteral("bus"),
            QStringLiteral("vectors"), QStringLiteral("globals"), QStringLiteral("rom"),
            QStringLiteral("rom-symbols"), QStringLiteral("screen"), QStringLiteral("sound"),
            QStringLiteral("profile"), QStringLiteral("load"), QStringLiteral("disk"),
            QStringLiteral("floppy"), QStringLiteral("mouse"), QStringLiteral("key"),
            QStringLiteral("sadmac"), QStringLiteral("arm"), QStringLiteral("report"),
            QStringLiteral("trace"), QStringLiteral("pc-trace"), QStringLiteral("trap-trace"),
            QStringLiteral("irq-trace"), QStringLiteral("driver-trace"), QStringLiteral("timeline"),
            QStringLiteral("bootblock"), QStringLiteral("gdb"), QStringLiteral("script"),
            QStringLiteral("paths"), QStringLiteral("quit"), QStringLiteral("exit"),
            QStringLiteral("on"), QStringLiteral("off"), QStringLiteral("status"),
            QStringLiteral("dump"), QStringLiteral("clear"), QStringLiteral("save"),
            QStringLiteral("export"), QStringLiteral("export-track"), QStringLiteral("export-window"),
            QStringLiteral("scan"), QStringLiteral("last-window"), QStringLiteral("verify"),
            QStringLiteral("probe"), QStringLiteral("hash"), QStringLiteral("capture-hash"),
            QStringLiteral("capture-export"), QStringLiteral("clear-capture"),
            QStringLiteral("insert"), QStringLiteral("eject"), QStringLiteral("read"),
            QStringLiteral("write"), QStringLiteral("rw"), QStringLiteral("all"),
            QStringLiteral("symbols"), QStringLiteral("load-symbols"),
        };
    }

    void installCompletion()
    {
        g_completionWords = commandWords();
        for (auto it = m_lowMemoryNames.cbegin(); it != m_lowMemoryNames.cend(); ++it) {
            g_completionWords.append(it.key());
        }
        for (auto it = m_symbols.cbegin(); it != m_symbols.cend(); ++it) {
            g_completionWords.append(it.key());
        }
        g_completionWords.removeDuplicates();
        g_completionWords.sort();
        rl_attempted_completion_function = commandCompletion;
    }

    void printBanner()
    {
        m_out << "CuteMacDebugSession\n";
        m_out << "profile: " << m_configuration.profileName << '\n';
        m_out << "type 'help' for commands\n";
        m_out.flush();
    }

    bool executeCommand(const QString& line)
    {
        const auto parts = splitCommand(line);
        if (parts.isEmpty()) {
            return true;
        }

        const auto command = parts[0].toLower();
        if (command == QStringLiteral("quit") || command == QStringLiteral("exit")) {
            return false;
        }
        const QSet<QString> machineNeutralCommands {
            QStringLiteral("help"), QStringLiteral("profile"), QStringLiteral("load"), QStringLiteral("reset"),
            QStringLiteral("run"), QStringLiteral("step"), QStringLiteral("run-until"), QStringLiteral("state"),
            QStringLiteral("regs"), QStringLiteral("disasm"), QStringLiteral("mem"), QStringLiteral("screen"), QStringLiteral("devices"),
            QStringLiteral("mouse"),
            QStringLiteral("sadmac"), QStringLiteral("paths"), QStringLiteral("quit"), QStringLiteral("exit")
        };
        if (m_iicxMachine != nullptr && !machineNeutralCommands.contains(command)) {
            m_out << command << " is not yet available for mac-iicx; common run/register/memory/video commands are available\n";
            m_out.flush();
            return true;
        }
        if (command == QStringLiteral("help")) {
            printHelp();
        } else if (command == QStringLiteral("profile")) {
            handleProfile(parts);
        } else if (command == QStringLiteral("load")) {
            loadProfile(parts);
        } else if (command == QStringLiteral("reset")) {
            reloadMachine();
        } else if (command == QStringLiteral("run")) {
            runCycles(parts);
        } else if (command == QStringLiteral("step")) {
            stepInstructions(parts);
        } else if (command == QStringLiteral("run-until")) {
            runUntil(parts);
        } else if (command == QStringLiteral("run-until-event")) {
            runUntilEvent(parts);
        } else if (command == QStringLiteral("state")) {
            printState();
        } else if (command == QStringLiteral("regs")) {
            printRegisters();
        } else if (command == QStringLiteral("disasm")) {
            disassemble(parts);
        } else if (command == QStringLiteral("mem")) {
            dumpMemory(parts);
        } else if (command == QStringLiteral("mem-find")) {
            findMemory(parts);
        } else if (command == QStringLiteral("mem-snapshot")) {
            snapshotMemory(parts);
        } else if (command == QStringLiteral("memory-diff")) {
            diffMemory(parts);
        } else if (command.startsWith(QStringLiteral("write"))) {
            writeMemory(command, parts);
        } else if (command == QStringLiteral("break")) {
            addBreakpoint(parts);
        } else if (command == QStringLiteral("delete")) {
            deleteBreakpoint(parts);
        } else if (command == QStringLiteral("breaks")) {
            printBreakpoints();
        } else if (command == QStringLiteral("watch")) {
            configureWatch(parts);
        } else if (command == QStringLiteral("bus")) {
            handleBus(parts);
        } else if (command == QStringLiteral("devices")) {
            printDevices(parts);
        } else if (command == QStringLiteral("vectors")) {
            printVectors();
        } else if (command == QStringLiteral("globals")) {
            printGlobals();
        } else if (command == QStringLiteral("lowmem")) {
            handleLowMemory(parts);
        } else if (command == QStringLiteral("screen")) {
            handleScreen(parts);
        } else if (command == QStringLiteral("sound")) {
            handleSound(parts);
        } else if (command == QStringLiteral("rom")) {
            handleRom(parts);
        } else if (command == QStringLiteral("disk")) {
            handleDisk(parts);
        } else if (command == QStringLiteral("floppy")) {
            handleFloppy(parts);
        } else if (command == QStringLiteral("mouse")) {
            handleMouse(parts);
        } else if (command == QStringLiteral("key")) {
            handleKey(parts);
        } else if (command == QStringLiteral("sadmac")) {
            handleSadMac(parts);
        } else if (command == QStringLiteral("trace")) {
            configureTrace(parts);
        } else if (command == QStringLiteral("pc-trace")) {
            dumpTraceRing(QStringLiteral("pc"), m_pcTrace, parts);
        } else if (command == QStringLiteral("trap-trace")) {
            dumpTraceRing(QStringLiteral("trap"), m_trapTrace, parts);
        } else if (command == QStringLiteral("irq-trace")) {
            dumpTraceRing(QStringLiteral("irq"), m_irqTrace, parts);
        } else if (command == QStringLiteral("driver-trace")) {
            dumpTraceRing(QStringLiteral("driver"), m_driverTrace, parts);
        } else if (command == QStringLiteral("timeline")) {
            dumpTraceRing(QStringLiteral("timeline"), m_timeline, parts);
        } else if (command == QStringLiteral("bootblock")) {
            handleBootBlock(parts);
        } else if (command == QStringLiteral("log")) {
            handleLog(parts);
        } else if (command == QStringLiteral("script")) {
            runScript(parts);
        } else if (command == QStringLiteral("gdb")) {
            configureGdb(parts);
        } else if (command == QStringLiteral("rom-symbols")) {
            handleSymbols(parts);
        } else if (command == QStringLiteral("paths")) {
            printPaths();
        } else {
            m_out << "unknown command: " << parts[0] << '\n';
        }

        m_out.flush();
        return true;
    }

    [[nodiscard]] QStringList splitCommand(const QString& line) const
    {
        QStringList parts;
        QString current;
        bool inQuote = false;

        for (const auto ch : line) {
            if (ch == QLatin1Char('"')) {
                inQuote = !inQuote;
                continue;
            }
            if (ch.isSpace() && !inQuote) {
                if (!current.isEmpty()) {
                    parts.append(current);
                    current.clear();
                }
                continue;
            }
            current.append(ch);
        }
        if (!current.isEmpty()) {
            parts.append(current);
        }
        return parts;
    }

    void printHelp()
    {
        m_out << "commands:\n";
        m_out << "  regs | state | devices [via|iwm|swim|scc|scsi|nubus]\n";
        m_out << "  step [count] | run [cycles] | run-until <addr> [max-cycles] | run-until-event floppy-eject [max-cycles]\n";
        m_out << "  disasm [addr|pc] [count] | mem <addr> [len]\n";
        m_out << "  mem-find <hex> [start len] | mem-snapshot <name> <addr> <len> | memory-diff <name>\n";
        m_out << "  write8|write16|write32 <addr> <value>\n";
        m_out << "  break <addr> | delete <addr|all> | breaks\n";
        m_out << "  watch read|write|rw <addr> [size]\n";
        m_out << "  bus [on|off|last [count]|clear|filter <region>]\n";
        m_out << "  vectors | globals | lowmem [watch|unwatch|status] [name]\n";
        m_out << "  rom info | rom-symbols [load <file>|list]\n";
        m_out << "  screen hash | screen probe | screen export <file.png>\n";
        m_out << "  sound hash | sound export <file.wav> | sound capture-hash | sound capture-export <file.wav> | sound clear-capture\n";
        m_out << "  profile [set <key> <value>|save] | load <profile.toml>\n";
        m_out << "  disk insert <path> | disk eject | disk status\n";
        m_out << "  floppy insert <path> | floppy eject | floppy status | floppy scan [track] [side] | floppy export-track <file> [track] [side]\n";
        m_out << "  mouse status | mouse move <x> <y> | mouse delta <dx> <dy> | mouse down|up\n";
        m_out << "  key status | key down <mac-code> | key up <mac-code> | key reset\n";
        m_out << "  trace [category on|off|dump|clear|save <file>] | pc-trace|trap-trace|irq-trace|driver-trace|timeline [count]\n";
        m_out << "  sadmac arm|run [max-cycles]|status|report|save <prefix>|clear\n";
        m_out << "  bootblock verify | floppy last-window | floppy export-window <file>\n";
        m_out << "  gdb [enable|disable|port N|start|stop|status]\n";
        m_out << "  script <file> | paths | quit\n";
    }

    void handleProfile(const QStringList& parts)
    {
        if (parts.size() == 1) {
            printProfile();
            return;
        }
        if (parts.size() >= 4 && parts[1] == QStringLiteral("set")) {
            setProfileValue(parts[2], parts.mid(3).join(QLatin1Char(' ')));
            return;
        }
        if (parts.size() == 2 && parts[1] == QStringLiteral("save")) {
            saveProfile();
            return;
        }
        m_out << "usage: profile [set <key> <value>|save]\n";
    }

    void printProfile()
    {
        m_out << "name=" << m_configuration.profileName << '\n';
        m_out << "machine=" << m_configuration.machineId << '\n';
        m_out << "rom=" << displayPath(m_configuration.romPath) << '\n';
        m_out << "disk=" << displayPath(m_configuration.diskPath) << '\n';
        m_out << "floppy=" << displayPath(m_configuration.floppyPath) << '\n';
        m_out << "ram_size_mib=" << (m_configuration.ramSizeKiB / 1024.0) << '\n';
        m_out << "cycles_per_frame=" << m_configuration.cyclesPerFrame << '\n';
        m_out << "skip_ram_pattern_test=" << (m_configuration.skipRamPatternTest ? "true" : "false") << '\n';
    }

    void setProfileValue(const QString& key, const QString& value)
    {
        if (key == QStringLiteral("name")) {
            m_configuration.profileName = value;
        } else if (key == QStringLiteral("rom_path")) {
            m_configuration.romPath = value;
            reloadMachine();
        } else if (key == QStringLiteral("disk_path")) {
            m_configuration.diskPath = value;
            if (!value.isEmpty()) {
                if (m_machine->loadDiskImage(value)) {
                    m_out << "disk loaded: " << displayPath(value) << '\n';
                } else {
                    m_out << "disk failed: " << displayPath(value) << '\n';
                }
            } else {
                m_machine->ejectDiskImage();
            }
        } else if (key == QStringLiteral("floppy_path")) {
            m_configuration.floppyPath = value;
            if (!value.isEmpty()) {
                if (m_machine->loadFloppyImage(value)) {
                    m_out << "floppy loaded: " << displayPath(value) << '\n';
                } else {
                    m_out << "floppy failed: " << displayPath(value) << '\n';
                }
            } else {
                m_machine->ejectFloppyImage();
            }
        } else if (key == QStringLiteral("cycles_per_frame")) {
            m_configuration.cyclesPerFrame = value.toInt();
        } else if (key == QStringLiteral("ram_size_mib")) {
            m_configuration.ramSizeKiB = qRound(value.toDouble() * 1024.0);
            reloadMachine();
        } else if (key == QStringLiteral("skip_ram_pattern_test")) {
            m_configuration.skipRamPatternTest = value.compare(QStringLiteral("true"), Qt::CaseInsensitive) == 0
                || value == QStringLiteral("1") || value.compare(QStringLiteral("on"), Qt::CaseInsensitive) == 0;
            reloadMachine();
        } else {
            m_out << "unknown profile key: " << key << '\n';
            return;
        }
        m_out << "profile " << key << " updated\n";
    }

    void saveProfile()
    {
        cutemac::config::ConfigurationManager manager;
        const auto path = manager.profilePathForName(m_configuration.profileName);
        if (manager.saveTomlFile(path, m_configuration)) {
            m_out << "saved " << path << '\n';
        } else {
            m_out << "failed to save profile\n";
        }
    }

    void loadProfile(const QStringList& parts)
    {
        if (parts.size() < 2) {
            m_out << "usage: load <profile.toml>\n";
            return;
        }

        cutemac::config::ConfigurationManager manager;
        const auto loaded = manager.loadTomlFile(parts[1]);
        if (!loaded.has_value()) {
            m_out << "failed to load profile: " << parts[1] << '\n';
            return;
        }

        m_configuration = *loaded;
        reloadMachine();
    }

    void reloadMachine()
    {
        m_sadMac = {};
        m_session = std::make_unique<cutemac::core::EmulationSession>(m_configuration);
        m_cpuDebug = m_session->debugCpuAccess();
        m_machine = static_cast<cutemac::machines::macplus::MacPlusMachine*>(m_session->debugMachine(QStringLiteral("mac-plus")));
        m_iicxMachine = static_cast<cutemac::machines::maciicx::MacIIcxMachine*>(m_session->debugMachine(QStringLiteral("mac-iicx")));
        m_powerMac8100Machine = static_cast<cutemac::machines::powermac8100::PowerMac8100Machine*>(
            m_session->debugMachine(QStringLiteral("powermac-8100")));
        if (m_cpuDebug == nullptr && m_machine == nullptr && m_iicxMachine == nullptr) {
            m_out << "debug support is unavailable for machine " << m_configuration.machineId << '\n';
            m_romLoaded = false;
            return;
        }
        if (m_machine != nullptr) {
            m_machine->setBusTraceEnabled(true);
            m_machine->setSoundCaptureEnabled(true);
            m_gdbStub = std::make_unique<GdbStub>(*m_machine, m_breakpoints);
            m_gdbStub->setEnabled(m_gdbEnabled);
            m_gdbStub->setPort(m_gdbPort);
        } else {
            m_gdbStub.reset();
        }

        m_romLoaded = m_session->initialize();
        if (!m_configuration.diskPath.isEmpty()) m_out << "disk loaded: " << displayPath(m_configuration.diskPath) << '\n';
        if (!m_configuration.floppyPath.isEmpty()) m_out << "floppy loaded: " << displayPath(m_configuration.floppyPath) << '\n';
        if (m_romLoaded) {
            m_out << "machine reset: cpu=" << (m_cpuDebug ? m_cpuDebug->debugCpuArchitecture() : QStringLiteral("m68k"))
                  << " pc=" << hexValue(debugProgramCounter()) << '\n';
        } else {
            const auto patchError = m_machine != nullptr ? m_machine->romInfo().patchError : QString {};
            if (!patchError.isEmpty()) {
                m_out << "ROM patch failed: " << patchError << '\n';
            } else {
                m_out << "machine reset without ROM; set rom_path in profile\n";
            }
        }
    }

    [[nodiscard]] std::uint32_t debugProgramCounter() const
    {
        if (m_cpuDebug) return m_cpuDebug->programCounter();
        return m_machine != nullptr ? m_machine->programCounter() : m_iicxMachine->programCounter();
    }

    [[nodiscard]] int debugRunCycles(int cycles)
    {
        if (m_cpuDebug) return m_cpuDebug->runCycles(cycles);
        return m_machine != nullptr ? m_machine->runCycles(cycles) : m_iicxMachine->runCycles(cycles);
    }

    [[nodiscard]] int debugStepInstruction()
    {
        if (m_cpuDebug) return m_cpuDebug->stepInstruction();
        return m_machine != nullptr ? m_machine->stepInstruction() : m_iicxMachine->runCycles(1);
    }

    [[nodiscard]] std::uint8_t debugRead8(std::uint32_t address) const
    {
        if (m_cpuDebug) return m_cpuDebug->debugRead8(address);
        return m_machine != nullptr ? m_machine->debugRead8(address) : m_iicxMachine->read8(address);
    }

    [[nodiscard]] std::uint16_t debugRead16(std::uint32_t address) const
    {
        return static_cast<std::uint16_t>((debugRead8(address) << 8) | debugRead8(address + 1));
    }

    [[nodiscard]] std::uint32_t debugRead32(std::uint32_t address) const
    {
        return (static_cast<std::uint32_t>(debugRead16(address)) << 16) | debugRead16(address + 2);
    }

    [[nodiscard]] std::uint32_t dereferenceHandle(std::uint32_t handle) const
    {
        return debugRead32(handle & 0x00ffffffU) & 0x00ffffffU;
    }

    [[nodiscard]] QString debugDisassemble(std::uint32_t address) const
    {
        if (m_cpuDebug) return m_cpuDebug->disassemble(address);
        return m_machine != nullptr ? m_machine->disassemble(address) : m_iicxMachine->disassemble(address);
    }

    [[nodiscard]] int debugDisassembleBytes(std::uint32_t address) const
    {
        if (m_cpuDebug) return m_cpuDebug->disassembleBytes(address);
        return m_machine != nullptr ? m_machine->disassembleBytes(address) : m_iicxMachine->disassembleBytes(address);
    }

    void runCycles(const QStringList& parts)
    {
        if (!requireRom()) {
            return;
        }
        const auto cycles = parts.size() >= 2 ? parts[1].toInt() : m_configuration.cyclesPerFrame;
        const auto cyclesRun = m_machine != nullptr && tracingRequiresStepping() ? runCyclesWithTracing(cycles) : debugRunCycles(cycles);
        m_out << "cycles_run=" << cyclesRun << " pc=" << hexValue(debugProgramCounter()) << '\n';
    }

    void stepInstructions(const QStringList& parts)
    {
        if (!requireRom()) {
            return;
        }
        const auto count = parts.size() >= 2 ? std::max(1, parts[1].toInt()) : 1;
        for (int i = 0; i < count; ++i) {
            if (m_breakpoints.count(debugProgramCounter()) != 0 && i != 0) {
                break;
            }
            if (m_machine != nullptr) sampleBeforeStep();
            (void)debugStepInstruction();
            if (m_machine != nullptr) sampleAfterStep();
        }
        m_out << "pc=" << hexValue(debugProgramCounter()) << ' ' << debugDisassemble(debugProgramCounter()) << '\n';
    }

    void runUntil(const QStringList& parts)
    {
        if (!requireRom()) {
            return;
        }
        if (parts.size() < 2) {
            m_out << "usage: run-until <addr> [max-cycles]\n";
            return;
        }
        const auto address = parseNumber(parts[1]);
        if (!address.has_value()) {
            m_out << "invalid address\n";
            return;
        }
        const auto maxCycles = parts.size() >= 3 ? parts[2].toInt() : 10000000;
        int cyclesUsed = 0;
        while (debugProgramCounter() != *address && cyclesUsed < maxCycles) {
            cyclesUsed += std::max(1, debugStepInstruction());
        }
        m_out << (debugProgramCounter() == *address ? "hit " : "timeout ") << hexValue(debugProgramCounter()) << '\n';
    }

    void runUntilEvent(const QStringList& parts)
    {
        if (!requireRom()) {
            return;
        }
        if (parts.size() < 2 || parts[1] != QStringLiteral("floppy-eject")) {
            m_out << "usage: run-until-event floppy-eject [max-cycles]\n";
            return;
        }
        if (m_machine->floppyImagePath().isEmpty()) {
            m_out << "no floppy inserted\n";
            return;
        }

        const auto maxCycles = parts.size() >= 3 ? parts[2].toInt() : 10000000;
        int cyclesUsed = 0;
        while (cyclesUsed < maxCycles && !m_machine->floppyImagePath().isEmpty()) {
            sampleBeforeStep();
            cyclesUsed += std::max(1, m_machine->stepInstruction());
            sampleAfterStep();
        }
        const auto ejected = m_machine->floppyImagePath().isEmpty();
        m_out << (ejected ? "event floppy-eject" : "timeout")
              << " cycles=" << cyclesUsed << " pc=" << hexValue(m_machine->programCounter()) << '\n';
    }

    void printState()
    {
        if (m_iicxMachine != nullptr) {
            const auto state = m_session->status();
            const auto io = m_iicxMachine->ioStatistics();
            m_out << "pc=" << hexValue(state.programCounter) << '\n';
            m_out << "overlay=" << (state.overlayEnabled ? "on" : "off") << '\n';
            m_out << "cycles=" << state.cycles << '\n';
            m_out << "nubus_reads=" << io.nubusReads << " nubus_writes=" << io.nubusWrites << '\n';
            m_out << "scsi_reads=" << io.scsiReads << " scsi_writes=" << io.scsiWrites << '\n';
            return;
        }
        const auto& summary = m_machine->accessSummary();
        m_out << "pc=" << hexValue(m_machine->programCounter()) << '\n';
        m_out << "overlay=" << (m_machine->overlayEnabled() ? "on" : "off") << '\n';
        m_out << "ram_reads=" << summary.ramReads << " ram_writes=" << summary.ramWrites << '\n';
        m_out << "rom_reads=" << summary.romReads << '\n';
        m_out << "configuration_reads=" << summary.configurationReads << '\n';
        m_out << "unmapped_reads=" << summary.unmappedReads << " unmapped_writes=" << summary.unmappedWrites << '\n';
    }

    void printRegisters()
    {
        if (m_cpuDebug) {
            m_out << "architecture=" << m_cpuDebug->debugCpuArchitecture() << '\n';
            for (const auto& line : m_cpuDebug->debugRegisterLines()) m_out << line << '\n';
            return;
        }
        const auto regs = m_machine != nullptr ? m_machine->cpuRegisters() : m_iicxMachine->cpuRegisters();
        for (int i = 0; i < 8; ++i) {
            m_out << "D" << i << '=' << hexValue(regs.d[i]) << ((i == 3 || i == 7) ? '\n' : ' ');
        }
        for (int i = 0; i < 8; ++i) {
            m_out << "A" << i << '=' << hexValue(regs.a[i]) << ((i == 3 || i == 7) ? '\n' : ' ');
        }
        m_out << "PC=" << hexValue(regs.pc) << " SR=" << hexValue(regs.sr, 4)
              << " USP=" << hexValue(regs.usp) << " ISP=" << hexValue(regs.isp)
              << " MSP=" << hexValue(regs.msp) << " VBR=" << hexValue(regs.vbr) << '\n';
    }

    void disassemble(const QStringList& parts)
    {
        auto address = debugProgramCounter();
        int count = 8;
        if (parts.size() >= 2 && parts[1] != QStringLiteral("pc")) {
            const auto parsed = parseNumber(parts[1]);
            if (!parsed.has_value()) {
                m_out << "invalid address\n";
                return;
            }
            address = *parsed;
        }
        if (parts.size() >= 3) {
            count = std::max(1, parts[2].toInt());
        }
        for (int i = 0; i < count; ++i) {
            const auto text = debugDisassemble(address);
            m_out << hexValue(address) << "  " << text << '\n';
            address += static_cast<std::uint32_t>(std::max(2, debugDisassembleBytes(address)));
        }
    }

    void dumpMemory(const QStringList& parts)
    {
        if (parts.size() < 2) {
            m_out << "usage: mem <addr> [len]\n";
            return;
        }
        const auto address = parseAddress(parts[1]);
        if (!address.has_value()) {
            m_out << "invalid address\n";
            return;
        }
        const auto length = parts.size() >= 3 ? std::max(1U, *parseNumber(parts[2])) : 128U;
        for (std::uint32_t offset = 0; offset < length; offset += 16) {
            m_out << hexValue(*address + offset) << " ";
            for (std::uint32_t i = 0; i < 16 && offset + i < length; ++i) {
                m_out << byteToHex(debugRead8(*address + offset + i)) << ' ';
            }
            m_out << '\n';
        }
    }

    void findMemory(const QStringList& parts)
    {
        if (parts.size() < 2) {
            m_out << "usage: mem-find <hex> [start len]\n";
            return;
        }
        const auto needle = hexToBytes(parts[1]);
        if (needle.isEmpty()) {
            m_out << "invalid hex pattern\n";
            return;
        }
        const auto start = parts.size() >= 3 ? parseAddress(parts[2]).value_or(0) : 0U;
        const auto defaultLength = static_cast<std::uint32_t>(std::max(1, m_configuration.ramSizeKiB)) * 1024;
        const auto length = parts.size() >= 4 ? parseNumber(parts[3]).value_or(defaultLength) : defaultLength;
        int hits = 0;
        for (std::uint32_t address = start; address + static_cast<std::uint32_t>(needle.size()) <= start + length; ++address) {
            bool match = true;
            for (qsizetype i = 0; i < needle.size(); ++i) {
                if (m_machine->debugRead8(address + static_cast<std::uint32_t>(i)) != static_cast<std::uint8_t>(needle[i])) {
                    match = false;
                    break;
                }
            }
            if (match) {
                m_out << "match " << hexValue(address) << '\n';
                if (++hits >= 32) {
                    m_out << "stopped after 32 matches\n";
                    break;
                }
            }
        }
        if (hits == 0) {
            m_out << "no matches\n";
        }
    }

    void snapshotMemory(const QStringList& parts)
    {
        if (parts.size() < 4) {
            m_out << "usage: mem-snapshot <name> <addr> <len>\n";
            return;
        }
        const auto address = parseAddress(parts[2]);
        const auto length = parseNumber(parts[3]);
        if (!address || !length) {
            m_out << "invalid address/length\n";
            return;
        }
        QByteArray bytes;
        bytes.resize(static_cast<qsizetype>(*length));
        for (std::uint32_t i = 0; i < *length; ++i) {
            bytes[static_cast<qsizetype>(i)] = static_cast<char>(m_machine->debugRead8(*address + i));
        }
        m_memorySnapshots[parts[1]] = { *address, bytes };
        m_out << "snapshot " << parts[1] << " bytes=" << bytes.size() << '\n';
    }

    void diffMemory(const QStringList& parts)
    {
        if (parts.size() < 2 || !m_memorySnapshots.contains(parts[1])) {
            m_out << "usage: memory-diff <name>\n";
            return;
        }
        const auto snapshot = m_memorySnapshots[parts[1]];
        int changes = 0;
        for (qsizetype i = 0; i < snapshot.bytes.size(); ++i) {
            const auto now = m_machine->debugRead8(snapshot.address + static_cast<std::uint32_t>(i));
            const auto was = static_cast<std::uint8_t>(snapshot.bytes[i]);
            if (now != was) {
                m_out << hexValue(snapshot.address + static_cast<std::uint32_t>(i)) << ' '
                      << byteToHex(was) << " -> " << byteToHex(now) << '\n';
                if (++changes >= 128) {
                    m_out << "stopped after 128 changes\n";
                    break;
                }
            }
        }
        if (changes == 0) {
            m_out << "no changes\n";
        }
    }

    void writeMemory(const QString& command, const QStringList& parts)
    {
        if (parts.size() < 3) {
            m_out << "usage: " << command << " <addr> <value>\n";
            return;
        }
        const auto address = parseNumber(parts[1]);
        const auto value = parseNumber(parts[2]);
        if (!address.has_value() || !value.has_value()) {
            m_out << "invalid address/value\n";
            return;
        }
        if (command == QStringLiteral("write8")) {
            m_machine->debugWrite8(*address, static_cast<std::uint8_t>(*value));
        } else if (command == QStringLiteral("write16")) {
            m_machine->debugWrite16(*address, static_cast<std::uint16_t>(*value));
        } else if (command == QStringLiteral("write32")) {
            m_machine->debugWrite32(*address, *value);
        } else {
            m_out << "unknown write command\n";
        }
    }

    void addBreakpoint(const QStringList& parts)
    {
        if (parts.size() < 2) {
            m_out << "usage: break <addr>\n";
            return;
        }
        const auto address = parseNumber(parts[1]);
        if (address.has_value()) {
            m_breakpoints.insert(*address & 0x00ffffff);
        }
        printBreakpoints();
    }

    void deleteBreakpoint(const QStringList& parts)
    {
        if (parts.size() < 2) {
            m_out << "usage: delete <addr|all>\n";
            return;
        }
        if (parts[1] == QStringLiteral("all")) {
            m_breakpoints.clear();
        } else if (const auto address = parseNumber(parts[1]); address.has_value()) {
            m_breakpoints.erase(*address & 0x00ffffff);
        }
        printBreakpoints();
    }

    void printBreakpoints()
    {
        if (m_breakpoints.empty()) {
            m_out << "no breakpoints\n";
            return;
        }
        for (const auto address : m_breakpoints) {
            m_out << "break " << hexValue(address) << '\n';
        }
    }

    void configureWatch(const QStringList& parts)
    {
        if (parts.size() < 3) {
            m_out << "usage: watch read|write|rw <addr> [size]\n";
            return;
        }
        m_watches.append(parts.mid(1).join(QLatin1Char(' ')));
        m_out << "watch added: " << m_watches.last() << '\n';
    }

    void handleBus(const QStringList& parts)
    {
        if (m_powerMac8100Machine != nullptr) {
            if (parts.size() >= 2 && (parts[1] == QStringLiteral("on") || parts[1] == QStringLiteral("off"))) {
                const auto enabled = parts[1] == QStringLiteral("on");
                m_powerMac8100Machine->setBusTraceEnabled(enabled);
                m_out << "bus trace=" << (enabled ? "on" : "off") << '\n';
                return;
            }
            int count = 32;
            bool amicOnly = false;
            bool scsiOnly = false;
            if (parts.size() >= 3 && parts[1] == QStringLiteral("filter"))
                amicOnly = parts[2].compare(QStringLiteral("amic"), Qt::CaseInsensitive) == 0;
            if (parts.size() >= 3 && parts[1] == QStringLiteral("filter"))
                scsiOnly = parts[2].compare(QStringLiteral("scsi"), Qt::CaseInsensitive) == 0;
            if (parts.size() >= 3 && parts[1] == QStringLiteral("last"))
                count = std::max(1, parts[2].toInt());
            const auto& trace = m_powerMac8100Machine->busTrace();
            const auto start = (amicOnly || scsiOnly) ? 0U
                : trace.size() > static_cast<std::size_t>(count) ? trace.size() - count : 0U;
            for (auto index = start; index < trace.size(); ++index) {
                const auto& access = trace[index];
                if (amicOnly && access.region
                    != cutemac::machines::powermac8100::PowerMac8100Machine::BusRegion::Amic) continue;
                if (scsiOnly) {
                    const auto controller = access.address >= 0x50f10000U
                        && access.address < 0x50f11200U;
                    const auto dma = access.address >= 0x50f32000U
                        && access.address < 0x50f32014U;
                    if (!controller && !dma) continue;
                }
                m_out << (access.write ? "write" : "read")
                      << " region=" << static_cast<unsigned>(access.region)
                      << " pc=" << hexValue(access.pc)
                      << " address=" << hexValue(access.address)
                      << " size=" << access.size
                      << " value=" << hexValue(access.value, access.size * 2) << '\n';
            }
            return;
        }
        if (parts.size() >= 2 && (parts[1] == QStringLiteral("on") || parts[1] == QStringLiteral("off"))) {
            const auto enabled = parts[1] == QStringLiteral("on");
            m_machine->setBusTraceEnabled(enabled);
            m_out << "bus trace=" << (enabled ? "on" : "off") << '\n';
            return;
        }
        if (parts.size() >= 2 && parts[1] == QStringLiteral("clear")) {
            m_machine->clearBusTrace();
            m_out << "bus trace cleared\n";
            return;
        }
        QString filter;
        int count = 32;
        if (parts.size() >= 3 && parts[1] == QStringLiteral("filter")) {
            filter = parts[2].toLower();
        } else if (parts.size() >= 3 && parts[1] == QStringLiteral("last")) {
            count = std::max(1, parts[2].toInt());
        }
        const auto trace = m_machine->busTrace();
        const auto start = std::max<qsizetype>(0, trace.size() - count);
        for (int i = start; i < trace.size(); ++i) {
            const auto& access = trace[i];
            if (!filter.isEmpty() && access.region != filter) {
                continue;
            }
            m_out << access.operation << ' ' << access.region << ' '
                  << hexValue(access.address, 6) << " size=" << access.size
                  << " value=" << hexValue(access.value, access.size * 2) << '\n';
        }
    }

    void printDevices(const QStringList& parts)
    {
        if (m_powerMac8100Machine != nullptr) {
            const auto device = parts.size() >= 2 ? parts[1].toLower() : QString();
            if (device.isEmpty() || device == QStringLiteral("scsi")) {
                const auto dma = m_powerMac8100Machine->scsiDmaDebugState();
                const auto irq = m_powerMac8100Machine->interruptDebugState();
                m_out << "scsi-dma base=" << hexValue(dma[0])
                      << " address=" << hexValue(dma[1])
                      << " offset=" << hexValue(dma[2])
                      << " control=" << hexValue(dma[3], 2)
                      << " via2_ifr=" << hexValue(irq[1], 2)
                      << " via2_ier=" << hexValue(irq[2], 2) << '\n';
                for (const auto internal : { false, true }) {
                    const auto& controller = m_powerMac8100Machine->scsiController(internal);
                    const auto scsi = controller.debugState();
                    m_out << "scsi-" << (internal ? "internal" : "external")
                          << " target=" << scsi.targetId
                          << " cdb=" << scsi.cdb.toHex(' ')
                          << " data=" << scsi.dataPosition << '/' << scsi.dataSize
                          << " remaining=" << scsi.transferCount
                          << " status=" << hexValue(scsi.status, 2)
                          << " interrupt=" << hexValue(scsi.interruptStatus, 2)
                          << " step=" << hexValue(scsi.sequenceStep, 2)
                          << " scsi_status=" << hexValue(scsi.scsiStatus, 2)
                          << " message=" << hexValue(scsi.message, 2)
                          << " phase=" << (scsi.command ? "command" : scsi.dataIn ? "data-in"
                                  : scsi.dataOut ? "data-out" : "other") << '\n';
                    m_out << "scsi-" << (internal ? "internal" : "external") << " commands";
                    for (std::size_t command = 0; command < controller.scsiCommandCounts().size(); ++command) {
                        if (controller.scsiCommandCounts()[command] != 0)
                            m_out << ' ' << hexValue(command, 2) << ':' << controller.scsiCommandCounts()[command];
                    }
                    m_out << '\n';
                }
            }
            return;
        }
        if (m_iicxMachine != nullptr) {
            const auto device = parts.size() >= 2 ? parts[1].toLower() : QString();
            const auto io = m_iicxMachine->ioStatistics();
            if (device.isEmpty() || device == QStringLiteral("nubus")) {
                m_out << "nubus_reads=" << io.nubusReads << " nubus_writes=" << io.nubusWrites << '\n';
                for (int slot = 9; slot <= 11; ++slot) {
                    const auto card = m_iicxMachine->nubusCard(slot);
                    if (!card) continue;
                    const auto frame = card->videoFrame();
                    m_out << "slot=" << slot << " card=" << card->id();
                    if (frame.width > 0 && frame.height > 0) {
                        m_out << " video=" << frame.width << 'x' << frame.height << " depth=" << frame.bitsPerPixel;
                        if (!frame.valid()) m_out << " invalid-frame";
                    }
                    if (const auto appleVideo = std::dynamic_pointer_cast<cutemac::devices::video::nubus::MacintoshIIVideoCard>(card)) {
                        m_out << " tfb=";
                        for (const auto value : appleVideo->timingRegisters()) m_out << hexValue(value, 2).mid(2);
                        m_out << " vbl=" << (appleVideo->vblEnabled() ? "enabled" : "disabled")
                              << " assertions=" << appleVideo->vblAssertions()
                              << " acks=" << appleVideo->vblAcks()
                              << " status_reads=" << appleVideo->vblStatusReads();
                    }
                    m_out << '\n';
                }
            }
            if (device.isEmpty() || device == QStringLiteral("via")) {
                const auto via1 = m_iicxMachine->via1DebugState();
                const auto via2 = m_iicxMachine->via2DebugState();
                m_out << "via1_ifr=" << hexValue(via1.interruptFlags, 2) << " ier=" << hexValue(via1.interruptEnable, 2)
                      << " irq=" << (via1.interruptActive ? "yes" : "no") << '\n';
                m_out << "via2_ifr=" << hexValue(via2.interruptFlags, 2) << " ier=" << hexValue(via2.interruptEnable, 2)
                      << " irq=" << (via2.interruptActive ? "yes" : "no") << '\n';
                const auto adb = m_iicxMachine->adbDebugState();
                m_out << "adb state=" << static_cast<int>(adb.state)
                      << " command=" << hexValue(adb.command, 2)
                      << " response=" << adb.responseBytes
                      << " cycles=" << adb.transferCycles
                      << " pending=" << (adb.commandPending ? "yes" : "no")
                      << " tx=" << (adb.transmittingFromVia ? "yes" : "no")
                      << " dx=" << adb.pendingMouseDx << " dy=" << adb.pendingMouseDy
                      << " keyboard=" << static_cast<int>(adb.keyboardAddress)
                      << " mouse=" << static_cast<int>(adb.mouseAddress) << '\n';
            }
            if (device.isEmpty() || device == QStringLiteral("scsi")) {
                m_out << "scsi_reads=" << io.scsiReads << " scsi_writes=" << io.scsiWrites << '\n';
                const auto scsi = m_iicxMachine->scsiDebugState();
                m_out << "scsi_phase=" << scsi.phase
                      << " target=" << (scsi.activeTargetId == 0xff ? QStringLiteral("none") : QString::number(scsi.activeTargetId))
                      << " selected=" << (scsi.selected ? "yes" : "no")
                      << " req=" << (scsi.request ? "yes" : "no")
                      << " ack=" << (scsi.ack ? "yes" : "no")
                      << " command_ready=" << (scsi.commandReady ? "yes" : "no") << '\n';
                m_out << "scsi_data index=" << scsi.dataIndex << '/' << scsi.dataLength
                      << " out=" << scsi.dataOutLength << '/' << scsi.expectedDataOutLength
                      << " completed=" << scsi.completedCommands << '\n';
                m_out << "scsi_status=" << hexValue(scsi.status, 2)
                      << " message=" << hexValue(scsi.message, 2)
                      << " tcmd=" << hexValue(scsi.targetCommand, 2)
                      << " output=" << hexValue(scsi.outputData, 2) << '\n';
                m_out << "scsi_active_cdb=" << scsi.activeCommand.toHex(' ') << '\n';
                m_out << "scsi_last_cdb=" << scsi.lastCommand.toHex(' ') << '\n';
            }
            if (device.isEmpty() || device == QStringLiteral("swim")) {
                m_out << "swim_reads=" << io.swimReads << " swim_writes=" << io.swimWrites << '\n';
                const auto swim = m_iicxMachine->swimDebugState();
                m_out << "swim_media=" << swim.imageFormat
                      << " inserted=" << (swim.diskInserted ? "yes" : "no")
                      << " density=" << (swim.highDensity ? "high" : "double")
                      << " motor=" << (swim.motorOn ? "on" : "off")
                      << " track=" << swim.track << " side=" << swim.side
                      << " data=" << swim.dataReads << " handshake=" << swim.handshakeReads
                      << " writes=" << swim.dataWrites << '\n';
            }
            return;
        }
        const auto& summary = m_machine->accessSummary();
        const auto device = parts.size() >= 2 ? parts[1].toLower() : QString();
        if (device.isEmpty() || device == QStringLiteral("via")) {
            m_out << "via_reads=" << summary.viaReads << " via_writes=" << summary.viaWrites << '\n';
            const auto via = m_machine->viaDebugState();
            m_out << "via_ifr=" << hexValue(via.interruptFlags, 2)
                  << " ier=" << hexValue(via.interruptEnable, 2)
                  << " irq=" << (via.interruptActive ? "yes" : "no")
                  << " t1=" << via.timer1Counter
                  << " t1_running=" << (via.timer1Running ? "yes" : "no")
                  << " t2=" << via.timer2Counter
                  << " t2_running=" << (via.timer2Running ? "yes" : "no") << '\n';
            m_out << "via_keyboard command=" << hexValue(via.keyboardCommand, 2)
                  << " cycles=" << via.keyboardCycles
                  << " queued=" << via.keyboardQueueDepth
                  << " pending=" << (via.keyboardCommandPending ? "yes" : "no")
                  << " response=" << (via.keyboardResponseReady ? "yes" : "no") << '\n';
        }
        if (device.isEmpty() || device == QStringLiteral("scc")) {
            m_out << "scc_reads=" << summary.sccReads << " scc_writes=" << summary.sccWrites << '\n';
        }
        if (device.isEmpty() || device == QStringLiteral("iwm")) {
            m_out << "iwm_reads=" << summary.iwmReads << " iwm_writes=" << summary.iwmWrites << '\n';
            const auto iwm = m_machine->iwmDebugState();
            m_out << "iwm_lines=" << hexValue(iwm.lines, 2)
                  << " mode=" << hexValue(iwm.mode, 2)
                  << " status=" << hexValue(iwm.status, 2)
                  << " reg=" << hexValue(iwm.selectedRegister, 2)
                  << " motor=" << (iwm.motorOn ? "on" : "off")
                  << " drive=" << (iwm.internalSelected ? "internal" : "external") << '\n';
            m_out << "floppy=" << displayPath(iwm.imagePath)
                  << " format=" << iwm.imageFormat
                  << " inserted=" << (iwm.diskInserted ? "yes" : "no")
                  << " writable=" << (iwm.writable ? "yes" : "no")
                  << " sides=" << (iwm.doubleSided ? "2" : "1")
                  << " track=" << iwm.track
                  << " side=" << iwm.side << '\n';
            m_out << "iwm_data_reads=" << iwm.dataReads
                  << " status_reads=" << iwm.statusReads
                  << " handshake_reads=" << iwm.handshakeReads
                  << " data_writes=" << iwm.dataWrites << '\n';
        }
        if (device.isEmpty() || device == QStringLiteral("scsi")) {
            m_out << "scsi_reads=" << summary.scsiReads << " scsi_writes=" << summary.scsiWrites << '\n';
            const auto scsi = m_machine->scsiDebugState();
            m_out << "scsi_disk=" << displayPath(m_machine->diskImagePath()) << '\n';
            m_out << "scsi_phase=" << scsi.phase
                  << " target=" << (scsi.activeTargetId == 0xff ? QStringLiteral("none") : QString::number(scsi.activeTargetId))
                  << " selected=" << (scsi.selected ? "yes" : "no")
                  << " req=" << (scsi.request ? "yes" : "no")
                  << " ack=" << (scsi.ack ? "yes" : "no")
                  << " command_ready=" << (scsi.commandReady ? "yes" : "no") << '\n';
            m_out << "scsi_data index=" << scsi.dataIndex << '/' << scsi.dataLength
                  << " out=" << scsi.dataOutLength << '/' << scsi.expectedDataOutLength
                  << " completed=" << scsi.completedCommands << '\n';
            m_out << "scsi_status=" << hexValue(scsi.status, 2)
                  << " message=" << hexValue(scsi.message, 2)
                  << " tcmd=" << hexValue(scsi.targetCommand, 2)
                  << " output=" << hexValue(scsi.outputData, 2) << '\n';
            m_out << "scsi_active_cdb=" << scsi.activeCommand.toHex(' ') << '\n';
            m_out << "scsi_last_cdb=" << scsi.lastCommand.toHex(' ') << '\n';
        }
        if (device.isEmpty()) {
            for (const auto& event : m_machine->eventLog()) {
                m_out << "event: " << event << '\n';
            }
        }
    }

    void printVectors()
    {
        for (std::uint32_t address = 0; address < 0x40; address += 4) {
            m_out << hexValue(address, 4) << " = " << hexValue(m_machine->debugRead32(address)) << '\n';
        }
    }

    void printGlobals()
    {
        const QMap<QString, std::uint32_t> globals {
            { QStringLiteral("MemTop"), 0x0108 },
            { QStringLiteral("BufPtr"), 0x010c },
            { QStringLiteral("Ticks"), 0x016a },
            { QStringLiteral("ScrnBase"), 0x0824 },
            { QStringLiteral("SoundBase"), 0x0266 },
            { QStringLiteral("ROMBase"), 0x02ae },
            { QStringLiteral("VIA"), 0x01d4 },
            { QStringLiteral("SCCRd"), 0x01d8 },
            { QStringLiteral("SCCWr"), 0x01dc },
            { QStringLiteral("IWM"), 0x01e0 },
        };
        for (auto it = globals.cbegin(); it != globals.cend(); ++it) {
            m_out << it.key() << " " << hexValue(it.value(), 4) << " = " << hexValue(m_machine->debugRead32(it.value())) << '\n';
        }
    }

    void handleScreen(const QStringList& parts)
    {
        const auto frame = m_session->videoFrame();
        const auto bytes = frame.pixels;
        std::uint32_t hash = 2166136261U;
        for (const auto byte : bytes) {
            hash ^= static_cast<std::uint8_t>(byte);
            hash *= 16777619U;
        }
        if (parts.size() >= 2 && parts[1] == QStringLiteral("hash")) {
            m_out << "screen_hash=" << hexValue(hash) << " size=" << frame.width << 'x' << frame.height
                  << " depth=" << frame.bitsPerPixel << '\n';
            return;
        }
        if (parts.size() >= 2 && parts[1] == QStringLiteral("probe")) {
            qsizetype zeroBytes = 0;
            qsizetype fullBytes = 0;
            for (const auto byte : bytes) {
                const auto value = static_cast<std::uint8_t>(byte);
                if (value == 0) ++zeroBytes;
                if (value == 0xff) ++fullBytes;
            }
            const auto label = bytes.isEmpty() ? QStringLiteral("invalid")
                : zeroBytes == bytes.size()    ? QStringLiteral("blank-zero")
                : fullBytes == bytes.size()    ? QStringLiteral("blank-full")
                                               : QStringLiteral("nonblank");
            appendTimeline(QStringLiteral("screen %1 hash=%2 zero=%3 full=%4").arg(label, hexValue(hash)).arg(zeroBytes).arg(fullBytes));
            m_out << "screen=" << label << " hash=" << hexValue(hash) << " zero_bytes=" << zeroBytes
                  << " full_bytes=" << fullBytes << " size=" << frame.width << 'x' << frame.height
                  << " depth=" << frame.bitsPerPixel << '\n';
            if (m_iicxMachine != nullptr) {
                const auto device = dereferenceHandle(debugRead32(0x08a8));
                const auto pixMapHandle = device == 0 ? 0 : debugRead32(device + 0x16);
                const auto pixMap = pixMapHandle == 0 ? 0 : dereferenceHandle(pixMapHandle);
                if (device != 0 && pixMap != 0) {
                    m_out << "gdevice=" << hexValue(device)
                          << " mode=" << hexValue(debugRead32(device + 0x2a))
                          << " pixmap=" << hexValue(pixMap)
                          << " base=" << hexValue(debugRead32(pixMap))
                          << " row_bytes=" << (debugRead16(pixMap + 4) & 0x3fffU)
                          << " pixel_size=" << debugRead16(pixMap + 0x20)
                          << " cmp_count=" << debugRead16(pixMap + 0x22)
                          << " cmp_size=" << debugRead16(pixMap + 0x24) << '\n';
                }
            }
            return;
        }
        if (parts.size() >= 3 && parts[1] == QStringLiteral("export")) {
            exportScreen(parts[2]);
            return;
        }
        m_out << "usage: screen hash | screen probe | screen export <file.png>\n";
    }

    void exportScreen(const QString& path)
    {
        const auto image = cutemac::session::FramebufferRenderer::render(m_session->videoFrame());
        m_out << (image.save(path) ? "saved " : "failed ") << path << '\n';
    }

    void handleSound(const QStringList& parts)
    {
        if (parts.size() >= 2 && parts[1] == QStringLiteral("hash")) {
            m_out << "sound_hash=" << hexValue(m_machine->soundBufferHash()) << '\n';
            return;
        }
        if (parts.size() >= 3 && parts[1] == QStringLiteral("export")) {
            exportSound(parts[2], m_machine->soundBufferBytes());
            return;
        }
        if (parts.size() >= 2 && parts[1] == QStringLiteral("capture-hash")) {
            m_out << "sound_capture_hash=" << hexValue(m_machine->soundCaptureHash())
                  << " bytes=" << m_machine->soundCaptureBytes().size() << '\n';
            return;
        }
        if (parts.size() >= 3 && parts[1] == QStringLiteral("capture-export")) {
            exportSound(parts[2], m_machine->soundCaptureBytes());
            return;
        }
        if (parts.size() >= 2 && parts[1] == QStringLiteral("clear-capture")) {
            m_machine->clearSoundCapture();
            m_out << "sound capture cleared\n";
            return;
        }
        m_out << "usage: sound hash | sound export <file.wav> | sound capture-hash | sound capture-export <file.wav> | sound clear-capture\n";
    }

    void exportSound(const QString& path, const QByteArray& samples)
    {
        constexpr std::uint32_t sampleRate = 22255;

        QByteArray wav;
        wav.append("RIFF", 4);
        appendLe32(wav, 36 + static_cast<std::uint32_t>(samples.size()));
        wav.append("WAVEfmt ", 8);
        appendLe32(wav, 16);
        appendLe16(wav, 1);
        appendLe16(wav, 1);
        appendLe32(wav, sampleRate);
        appendLe32(wav, sampleRate);
        appendLe16(wav, 1);
        appendLe16(wav, 8);
        wav.append("data", 4);
        appendLe32(wav, static_cast<std::uint32_t>(samples.size()));
        wav.append(samples);

        QSaveFile file(path);
        if (!file.open(QIODevice::WriteOnly)) {
            m_out << "failed " << path << '\n';
            return;
        }
        file.write(wav);
        m_out << (file.commit() ? "saved " : "failed ") << path << '\n';
    }

    void handleRom(const QStringList& parts)
    {
        if (parts.size() >= 2 && parts[1] != QStringLiteral("info")) {
            m_out << "usage: rom info\n";
            return;
        }
        const auto info = m_machine->romInfo();
        m_out << "rom_loaded=" << (info.loaded ? "yes" : "no") << '\n';
        m_out << "rom_path=" << displayPath(info.path) << '\n';
        m_out << "rom_size=" << info.size << '\n';
        m_out << "rom_checksum=" << hexValue(info.checksum) << '\n';
        m_out << "reset_sp=" << hexValue(info.resetStackPointer) << '\n';
        m_out << "reset_pc=" << hexValue(info.resetProgramCounter) << '\n';
        m_out << "rom_sha256=" << info.sha256 << '\n';
        m_out << "rom_patches=" << (info.appliedPatches.isEmpty() ? QStringLiteral("none") : info.appliedPatches.join(QLatin1Char(','))) << '\n';
        if (!info.patchError.isEmpty()) {
            m_out << "rom_patch_error=" << info.patchError << '\n';
        }
    }

    void handleDisk(const QStringList& parts)
    {
        if (parts.size() >= 3 && parts[1] == QStringLiteral("insert")) {
            m_configuration.diskPath = parts[2];
            if (m_machine->loadDiskImage(m_configuration.diskPath)) {
                m_out << "disk inserted: " << displayPath(m_configuration.diskPath) << '\n';
            } else {
                m_out << "disk insert failed: " << displayPath(m_configuration.diskPath) << '\n';
            }
        } else if (parts.size() >= 2 && parts[1] == QStringLiteral("eject")) {
            m_configuration.diskPath.clear();
            m_machine->ejectDiskImage();
            m_out << "disk ejected\n";
        } else if (parts.size() >= 2 && parts[1] == QStringLiteral("status")) {
            m_out << "disk=" << displayPath(m_machine->diskImagePath()) << '\n';
        } else {
            m_out << "usage: disk insert <path> | disk eject | disk status\n";
        }
    }

    void handleFloppy(const QStringList& parts)
    {
        if (parts.size() >= 3 && parts[1] == QStringLiteral("insert")) {
            m_configuration.floppyPath = parts[2];
            if (m_machine->loadFloppyImage(m_configuration.floppyPath)) {
                m_out << "floppy inserted: " << displayPath(m_configuration.floppyPath) << '\n';
            } else {
                m_out << "floppy insert failed: " << displayPath(m_configuration.floppyPath) << '\n';
            }
        } else if (parts.size() >= 2 && parts[1] == QStringLiteral("eject")) {
            m_configuration.floppyPath.clear();
            m_machine->ejectFloppyImage();
            m_out << "floppy ejected\n";
        } else if (parts.size() >= 2 && parts[1] == QStringLiteral("status")) {
            const auto iwm = m_machine->iwmDebugState();
            m_out << "floppy=" << displayPath(iwm.imagePath)
                  << " format=" << iwm.imageFormat
                  << " inserted=" << (iwm.diskInserted ? "yes" : "no")
                  << " track=" << iwm.track
                  << " side=" << iwm.side
                  << " track_bytes=" << iwm.trackBytes
                  << " cursor=" << iwm.trackCursor
                  << " motor=" << (iwm.motorOn ? "on" : "off") << '\n';
        } else if (parts.size() >= 2 && parts[1] == QStringLiteral("scan")) {
            const auto iwm = m_machine->iwmDebugState();
            const auto track = parts.size() >= 3 ? parseNumber(parts[2]).value_or(static_cast<std::uint32_t>(iwm.track)) : static_cast<std::uint32_t>(iwm.track);
            const auto side = parts.size() >= 4 ? parseNumber(parts[3]).value_or(static_cast<std::uint32_t>(iwm.side)) : static_cast<std::uint32_t>(iwm.side);
            const auto bytes = m_machine->floppyTrackBytesForDebug(static_cast<int>(track), static_cast<int>(side));
            m_out << "track=" << track
                  << " side=" << side
                  << " bytes=" << bytes.size()
                  << " addr_marks=" << countPattern(bytes, QByteArray::fromHex("d5aa96"))
                  << " data_marks=" << countPattern(bytes, QByteArray::fromHex("d5aaad"))
                  << " trailers=" << countPattern(bytes, QByteArray::fromHex("deaa")) << '\n';
        } else if (parts.size() >= 3 && parts[1] == QStringLiteral("export-track")) {
            const auto iwm = m_machine->iwmDebugState();
            const auto track = parts.size() >= 4 ? parseNumber(parts[3]).value_or(static_cast<std::uint32_t>(iwm.track)) : static_cast<std::uint32_t>(iwm.track);
            const auto side = parts.size() >= 5 ? parseNumber(parts[4]).value_or(static_cast<std::uint32_t>(iwm.side)) : static_cast<std::uint32_t>(iwm.side);
            const auto bytes = m_machine->floppyTrackBytesForDebug(static_cast<int>(track), static_cast<int>(side));
            QSaveFile file(parts[2]);
            if (!file.open(QIODevice::WriteOnly)) {
                m_out << "floppy export failed: " << parts[2] << '\n';
                return;
            }
            file.write(bytes);
            if (!file.commit()) {
                m_out << "floppy export failed: " << parts[2] << '\n';
                return;
            }
            m_out << "floppy track exported: " << parts[2] << " bytes=" << bytes.size() << '\n';
        } else if (parts.size() >= 2 && parts[1] == QStringLiteral("last-window")) {
            const auto bytes = m_machine->iwmLastNibblesForDebug();
            m_out << "floppy_window_bytes=" << bytes.size()
                  << " addr_marks=" << countPattern(bytes, QByteArray::fromHex("d5aa96"))
                  << " data_marks=" << countPattern(bytes, QByteArray::fromHex("d5aaad"))
                  << " trailers=" << countPattern(bytes, QByteArray::fromHex("deaa")) << '\n';
        } else if (parts.size() >= 3 && parts[1] == QStringLiteral("export-window")) {
            QSaveFile file(parts[2]);
            if (!file.open(QIODevice::WriteOnly)) {
                m_out << "floppy window export failed: " << parts[2] << '\n';
                return;
            }
            file.write(m_machine->iwmLastNibblesForDebug());
            m_out << (file.commit() ? "floppy window exported: " : "floppy window export failed: ") << parts[2] << '\n';
        } else {
            m_out << "usage: floppy insert <path> | floppy eject | floppy status | floppy scan [track] [side] | floppy export-track <file> [track] [side] | floppy last-window | floppy export-window <file>\n";
        }
    }

    void handleMouse(const QStringList& parts)
    {
        if (parts.size() == 1 || parts[1] == QStringLiteral("status")) {
            const auto x = m_machine ? m_machine->mouseX() : m_debugMouseX;
            const auto y = m_machine ? m_machine->mouseY() : m_debugMouseY;
            const auto pressed = m_machine ? m_machine->mouseButtonPressed() : m_debugMouseButton;
            m_out << "mouse x=" << x << " y=" << y
                  << " button=" << (pressed ? "down" : "up") << '\n';
            return;
        }
        if (parts.size() >= 4 && parts[1] == QStringLiteral("move")) {
            const auto x = parseNumber(parts[2]);
            const auto y = parseNumber(parts[3]);
            if (!x || !y) {
                m_out << "invalid mouse coordinates\n";
                return;
            }
            m_debugMouseX = static_cast<std::int16_t>(*x);
            m_debugMouseY = static_cast<std::int16_t>(*y);
            if (m_machine) m_machine->setMousePosition(m_debugMouseX, m_debugMouseY);
            else m_session->queueMousePosition(m_debugMouseX, m_debugMouseY);
            handleMouse({ QStringLiteral("mouse"), QStringLiteral("status") });
            return;
        }
        if (parts.size() >= 4 && parts[1] == QStringLiteral("delta")) {
            const auto dx = parts[2].toInt();
            const auto dy = parts[3].toInt();
            m_debugMouseX = static_cast<std::int16_t>(m_debugMouseX + dx);
            m_debugMouseY = static_cast<std::int16_t>(m_debugMouseY + dy);
            if (m_machine) m_machine->moveMouse(static_cast<std::int16_t>(dx), static_cast<std::int16_t>(dy));
            else m_session->queueMouseDelta(static_cast<std::int16_t>(dx), static_cast<std::int16_t>(dy));
            handleMouse({ QStringLiteral("mouse"), QStringLiteral("status") });
            return;
        }
        if (parts.size() >= 2 && (parts[1] == QStringLiteral("down") || parts[1] == QStringLiteral("up"))) {
            m_debugMouseButton = parts[1] == QStringLiteral("down");
            if (m_machine) m_machine->setMouseButton(m_debugMouseButton);
            else m_session->queueMouseButton(m_debugMouseButton);
            handleMouse({ QStringLiteral("mouse"), QStringLiteral("status") });
            return;
        }
        m_out << "usage: mouse status | mouse move <x> <y> | mouse delta <dx> <dy> | mouse down|up\n";
    }

    void handleKey(const QStringList& parts)
    {
        if (parts.size() == 1 || parts[1] == QStringLiteral("status")) {
            if (m_machine) m_out << "keymap=" << bytesToHex(m_machine->keyMapBytes()) << '\n';
            else m_out << "keymap unavailable for this machine\n";
            return;
        }
        if (parts.size() >= 2 && parts[1] == QStringLiteral("reset")) {
            if (m_machine) m_machine->resetKeyboard();
            else m_session->queueKeyboardReset();
            m_out << "keyboard reset\n";
            return;
        }
        if (parts.size() >= 3 && (parts[1] == QStringLiteral("down") || parts[1] == QStringLiteral("up"))) {
            const auto code = parseNumber(parts[2]);
            if (!code || *code > 0x7f) {
                m_out << "invalid Mac key code\n";
                return;
            }
            if (m_machine) m_machine->setKeyState(static_cast<std::uint8_t>(*code), parts[1] == QStringLiteral("down"));
            else m_session->queueKey(static_cast<std::uint8_t>(*code), parts[1] == QStringLiteral("down"));
            handleKey({ QStringLiteral("key"), QStringLiteral("status") });
            return;
        }
        m_out << "usage: key status | key down <mac-code> | key up <mac-code> | key reset\n";
    }

    void configureTrace(const QStringList& parts)
    {
        if (parts.size() == 1) {
            printTraceStatus();
            return;
        }
        if (parts.size() >= 2 && parts[1] == QStringLiteral("clear")) {
            clearTraces();
            m_out << "traces cleared\n";
            return;
        }
        if (parts.size() >= 2 && parts[1] == QStringLiteral("dump")) {
            dumpTraceRing(QStringLiteral("timeline"), m_timeline, parts);
            return;
        }
        if (parts.size() >= 3 && parts[1] == QStringLiteral("save")) {
            saveTrace(parts[2]);
            return;
        }
        if (parts.size() != 3) {
            m_out << "usage: trace [category on|off|dump|clear|save <file>]\n";
            return;
        }
        setTraceCategory(parts[1].toLower(), parts[2].compare(QStringLiteral("on"), Qt::CaseInsensitive) == 0);
    }

    void printTraceStatus()
    {
        m_out << "trace pc=" << onOff(m_trace.pc)
              << " irq=" << onOff(m_trace.irq)
              << " trap=" << onOff(m_trace.trap)
              << " driver=" << onOff(m_trace.driver)
              << " lowmem=" << onOff(m_trace.lowmem)
              << " screen=" << onOff(m_trace.screen)
              << " sound=" << onOff(m_trace.sound)
              << " iwm=" << onOff(m_trace.iwm)
              << " floppy=" << onOff(m_trace.floppy)
              << " timeline=" << onOff(m_trace.timeline) << '\n';
    }

    void setTraceCategory(const QString& category, bool enabled)
    {
        if (category == QStringLiteral("pc")) {
            m_trace.pc = enabled;
        } else if (category == QStringLiteral("irq")) {
            m_trace.irq = enabled;
        } else if (category == QStringLiteral("trap")) {
            m_trace.trap = enabled;
        } else if (category == QStringLiteral("driver")) {
            m_trace.driver = enabled;
        } else if (category == QStringLiteral("lowmem")) {
            m_trace.lowmem = enabled;
        } else if (category == QStringLiteral("screen")) {
            m_trace.screen = enabled;
        } else if (category == QStringLiteral("sound")) {
            m_trace.sound = enabled;
        } else if (category == QStringLiteral("iwm")) {
            m_trace.iwm = enabled;
            m_machine->setIwmTraceEnabled(m_trace.iwm || m_trace.floppy);
        } else if (category == QStringLiteral("floppy")) {
            m_trace.floppy = enabled;
            m_machine->setIwmTraceEnabled(m_trace.iwm || m_trace.floppy);
        } else if (category == QStringLiteral("timeline")) {
            m_trace.timeline = enabled;
        } else if (category == QStringLiteral("all")) {
            m_trace = { enabled, enabled, enabled, enabled, enabled, enabled, enabled, enabled, enabled, enabled };
            m_machine->setIwmTraceEnabled(enabled);
        } else {
            m_out << "unknown trace category: " << category << '\n';
            return;
        }
        m_out << "trace " << category << '=' << (enabled ? "on" : "off") << '\n';
    }

    void dumpTraceRing(const QString& name, const QStringList& ring, const QStringList& parts)
    {
        const auto count = parts.size() >= 2 ? std::max(1, parts.last().toInt()) : 64;
        const auto start = std::max<qsizetype>(0, ring.size() - count);
        for (qsizetype i = start; i < ring.size(); ++i) {
            m_out << name << ": " << ring[i] << '\n';
        }
        if (name == QStringLiteral("timeline") || name == QStringLiteral("iwm")) {
            for (const auto& event : m_machine->iwmTraceEvents()) {
                m_out << "iwm: " << event << '\n';
            }
        }
    }

    void clearTraces()
    {
        m_pcTrace.clear();
        m_irqTrace.clear();
        m_trapTrace.clear();
        m_driverTrace.clear();
        m_timeline.clear();
        m_machine->clearIwmTrace();
    }

    void saveTrace(const QString& path)
    {
        QSaveFile file(path);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
            m_out << "failed to open trace file\n";
            return;
        }
        QTextStream stream(&file);
        for (const auto& event : m_timeline) {
            stream << "{\"category\":\"timeline\",\"event\":\"" << event << "\"}\n";
        }
        for (const auto& event : m_pcTrace) {
            stream << "{\"category\":\"pc\",\"event\":\"" << event << "\"}\n";
        }
        for (const auto& event : m_irqTrace) {
            stream << "{\"category\":\"irq\",\"event\":\"" << event << "\"}\n";
        }
        for (const auto& event : m_trapTrace) {
            stream << "{\"category\":\"trap\",\"event\":\"" << event << "\"}\n";
        }
        for (const auto& event : m_driverTrace) {
            stream << "{\"category\":\"driver\",\"event\":\"" << event << "\"}\n";
        }
        for (const auto& event : m_machine->iwmTraceEvents()) {
            stream << "{\"category\":\"iwm\",\"event\":\"" << event << "\"}\n";
        }
        m_out << (file.commit() ? "saved " : "failed ") << path << '\n';
    }

    void handleLowMemory(const QStringList& parts)
    {
        if (parts.size() == 1 || parts[1] == QStringLiteral("status")) {
            for (auto it = m_lowMemoryNames.cbegin(); it != m_lowMemoryNames.cend(); ++it) {
                m_out << it.key() << ' ' << hexValue(it.value(), 4) << " = " << hexValue(m_machine->debugRead32(it.value())) << '\n';
            }
            m_out << '\n';
            return;
        }
        if (parts.size() >= 3 && parts[1] == QStringLiteral("watch")) {
            const auto address = parseAddress(parts[2]);
            if (!address) {
                m_out << "unknown lowmem name/address\n";
                return;
            }
            m_lowMemoryWatchValues[parts[2]] = m_machine->debugRead32(*address);
            m_out << "lowmem watch " << parts[2] << '\n';
            return;
        }
        if (parts.size() >= 3 && parts[1] == QStringLiteral("unwatch")) {
            m_lowMemoryWatchValues.remove(parts[2]);
            m_out << "lowmem unwatch " << parts[2] << '\n';
            return;
        }
        m_out << "usage: lowmem [status|watch <name|addr>|unwatch <name|addr>]\n";
    }

    void handleLog(const QStringList& parts)
    {
        if (parts.size() < 3 || parts[1] != QStringLiteral("save")) {
            m_out << "usage: log save <file.jsonl>\n";
            return;
        }
        QSaveFile file(parts[2]);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
            m_out << "failed to open log\n";
            return;
        }
        QTextStream stream(&file);
        for (const auto& access : m_machine->busTrace()) {
            stream << "{\"op\":\"" << access.operation << "\",\"region\":\"" << access.region
                   << "\",\"address\":" << access.address << ",\"value\":" << access.value
                   << ",\"size\":" << access.size << "}\n";
        }
        m_out << (file.commit() ? "saved " : "failed ") << parts[2] << '\n';
    }

    void handleBootBlock(const QStringList& parts)
    {
        if (parts.size() < 2 || parts[1] != QStringLiteral("verify")) {
            m_out << "usage: bootblock verify\n";
            return;
        }
        QFile file(m_configuration.floppyPath);
        if (!file.open(QIODevice::ReadOnly)) {
            m_out << "no readable floppy image configured\n";
            return;
        }
        const auto image = file.readAll();
        QByteArray data;
        if (image.size() >= 84 && (readBe32FromBytes(image, 64) == 400 * 1024 || readBe32FromBytes(image, 64) == 800 * 1024)) {
            data = image.mid(84, 1024);
        } else {
            data = image.left(1024);
        }
        if (data.size() < 1024) {
            m_out << "floppy image does not contain boot blocks\n";
            return;
        }
        const auto block0 = data.left(512);
        const auto block1 = data.mid(512, 512);
        const auto block0Needle = block0.left(32).toHex();
        const auto block1Needle = block1.left(32).toHex();
        m_out << "bootblock0 first32=" << block0Needle << '\n';
        findNeedleInRam(QByteArray::fromHex(block0Needle), QStringLiteral("bootblock0"));
        m_out << "bootblock1 first32=" << block1Needle << '\n';
        findNeedleInRam(QByteArray::fromHex(block1Needle), QStringLiteral("bootblock1"));
    }

    void findNeedleInRam(const QByteArray& needle, const QString& label)
    {
        const auto length = static_cast<std::uint32_t>(std::max(1, m_configuration.ramSizeKiB)) * 1024;
        int hits = 0;
        for (std::uint32_t address = 0; address + static_cast<std::uint32_t>(needle.size()) <= length; ++address) {
            bool match = true;
            for (qsizetype i = 0; i < needle.size(); ++i) {
                if (m_machine->debugRead8(address + static_cast<std::uint32_t>(i)) != static_cast<std::uint8_t>(needle[i])) {
                    match = false;
                    break;
                }
            }
            if (match) {
                m_out << label << " loaded_at=" << hexValue(address) << '\n';
                ++hits;
            }
        }
        if (hits == 0) {
            m_out << label << " not found in RAM\n";
        }
    }

    void handleSymbols(const QStringList& parts)
    {
        if (parts.size() == 1 || parts[1] == QStringLiteral("list")) {
            for (auto it = m_symbols.cbegin(); it != m_symbols.cend(); ++it) {
                m_out << it.key() << ' ' << hexValue(it.value()) << '\n';
            }
            return;
        }
        if (parts.size() >= 3 && parts[1] == QStringLiteral("load")) {
            QFile file(parts[2]);
            if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
                m_out << "failed to open symbols\n";
                return;
            }
            QRegularExpression linePattern(QStringLiteral("^\\s*([A-Za-z_.$][A-Za-z0-9_.$]*)\\s+(0x[0-9A-Fa-f]+|\\$[0-9A-Fa-f]+|[0-9]+)"));
            int loaded = 0;
            while (!file.atEnd()) {
                const auto line = QString::fromUtf8(file.readLine());
                const auto match = linePattern.match(line);
                if (!match.hasMatch()) {
                    continue;
                }
                const auto address = parseNumber(match.captured(2));
                if (address) {
                    m_symbols[match.captured(1)] = *address & 0x00ffffff;
                    ++loaded;
                }
            }
            installCompletion();
            m_out << "symbols loaded=" << loaded << '\n';
            return;
        }
        m_out << "usage: rom-symbols [load <file>|list]\n";
    }

    void runScript(const QStringList& parts)
    {
        if (parts.size() < 2) {
            m_out << "usage: script <file>\n";
            return;
        }
        QFile file(parts[1]);
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            m_out << "failed to open script\n";
            return;
        }
        while (!file.atEnd()) {
            const auto line = QString::fromUtf8(file.readLine()).trimmed();
            if (line.isEmpty() || line.startsWith(QLatin1Char('#'))) {
                continue;
            }
            m_out << "script> " << line << '\n';
            if (!executeCommand(line)) {
                break;
            }
        }
    }

    void configureGdb(const QStringList& parts)
    {
        if (parts.size() == 1 || parts[1] == QStringLiteral("status")) {
            printGdb();
            return;
        }

        const auto subcommand = parts[1].toLower();
        if (subcommand == QStringLiteral("enable")) {
            m_gdbEnabled = true;
            m_gdbStub->setEnabled(true);
        } else if (subcommand == QStringLiteral("disable") || subcommand == QStringLiteral("stop")) {
            m_gdbEnabled = false;
            m_gdbStub->setEnabled(false);
        } else if (subcommand == QStringLiteral("port") && parts.size() >= 3) {
            bool ok = false;
            const auto port = parts[2].toUShort(&ok);
            if (ok) {
                m_gdbPort = port;
                m_gdbStub->setPort(port);
            } else {
                m_out << "invalid port\n";
            }
        } else if (subcommand == QStringLiteral("start")) {
            m_gdbEnabled = true;
            m_gdbStub->setEnabled(true);
            m_gdbStub->serve(m_out);
        } else {
            m_out << "usage: gdb [enable|disable|port N|start|stop|status]\n";
            return;
        }
        printGdb();
    }

    void printGdb()
    {
        m_out << "gdb_stub=" << (m_gdbEnabled ? "enabled" : "disabled") << " port=" << m_gdbPort << '\n';
    }

    void armSadMac()
    {
        m_sadMac = {};
        m_sadMac.armed = true;
        m_out << "sadmac armed architecture="
              << (m_cpuDebug ? m_cpuDebug->debugCpuArchitecture() : QStringLiteral("unknown")) << '\n';
    }

    void sampleSadMacInstruction()
    {
        const auto pc = debugProgramCounter();
        appendRing(m_sadMac.instructions, QStringLiteral("cycle=%1 pc=%2 %3")
            .arg(m_session->status().cycles).arg(hexValue(pc), debugDisassemble(pc)));
    }

    bool detectSadMac()
    {
        if (!m_sadMac.armed || !cutemac::debug::SadMacDetector::detect(m_session->videoFrame())) return false;
        const auto status = m_session->status();
        m_sadMac.armed = false;
        m_sadMac.detected = true;
        m_sadMac.cycle = status.cycles;
        m_sadMac.pc = debugProgramCounter();
        m_sadMac.reason = QStringLiteral("framebuffer-layout");
        m_sadMac.frame = m_session->videoFrame();
        if (m_cpuDebug) m_sadMac.registers = m_cpuDebug->debugRegisterLines();
        if (m_powerMac8100Machine != nullptr) {
            const auto registers = m_powerMac8100Machine->cpuRegisters();
            // The PDM ROM's 68k compatibility renderer carries the displayed
            // primary and secondary words in these nanokernel registers.
            m_sadMac.primaryCode = registers.gpr[15];
            m_sadMac.secondaryCode = registers.gpr[14];
        }
        m_out << "sadmac detected cycle=" << m_sadMac.cycle << " pc=" << hexValue(m_sadMac.pc)
              << " reason=" << m_sadMac.reason << '\n';
        return true;
    }

    void reportSadMac(QTextStream& out, int instructionCount = 64) const
    {
        out << "sadmac detected=" << (m_sadMac.detected ? "yes" : "no")
            << " armed=" << (m_sadMac.armed ? "yes" : "no") << '\n';
        if (!m_sadMac.detected) return;
        out << "machine=" << m_configuration.machineId << " architecture="
            << (m_cpuDebug ? m_cpuDebug->debugCpuArchitecture() : QStringLiteral("unknown")) << '\n';
        out << "cycle=" << m_sadMac.cycle << " pc=" << hexValue(m_sadMac.pc)
            << " reason=" << m_sadMac.reason << '\n';
        if (m_sadMac.primaryCode && m_sadMac.secondaryCode)
            out << "codes primary=" << hexValue(*m_sadMac.primaryCode)
                << " secondary=" << hexValue(*m_sadMac.secondaryCode) << '\n';
        out << "frame=" << m_sadMac.frame.width << 'x' << m_sadMac.frame.height
            << "x" << m_sadMac.frame.bitsPerPixel << " stride=" << m_sadMac.frame.strideBytes << '\n';
        for (const auto& line : m_sadMac.registers) out << "register " << line << '\n';
        const auto begin = std::max<qsizetype>(0, m_sadMac.instructions.size() - instructionCount);
        for (qsizetype i = begin; i < m_sadMac.instructions.size(); ++i)
            out << "instruction " << m_sadMac.instructions[i] << '\n';
    }

    void handleSadMac(const QStringList& parts)
    {
        const auto subcommand = parts.size() >= 2 ? parts[1].toLower() : QStringLiteral("status");
        if (subcommand == QStringLiteral("arm")) {
            armSadMac();
        } else if (subcommand == QStringLiteral("clear")) {
            m_sadMac = {};
            m_out << "sadmac capture cleared\n";
        } else if (subcommand == QStringLiteral("status")) {
            reportSadMac(m_out, 0);
        } else if (subcommand == QStringLiteral("report")) {
            reportSadMac(m_out);
        } else if (subcommand == QStringLiteral("run")) {
            if (!requireRom()) return;
            if (!m_sadMac.armed) armSadMac();
            const auto maxCycles = parts.size() >= 3 ? std::max(1, parts[2].toInt()) : 100'000'000;
            int cycles = 0;
            unsigned samples = 0;
            while (cycles < maxCycles && m_sadMac.armed) {
                sampleSadMacInstruction();
                cycles += std::max(1, debugStepInstruction());
                if ((++samples & 0xffU) == 0 && detectSadMac()) break;
            }
            if (m_sadMac.armed) (void)detectSadMac();
            m_out << (m_sadMac.detected ? "sadmac stopped" : "sadmac timeout")
                  << " cycles=" << cycles << " pc=" << hexValue(debugProgramCounter()) << '\n';
        } else if (subcommand == QStringLiteral("save")) {
            if (!m_sadMac.detected || parts.size() < 3) {
                m_out << "usage: sadmac save <prefix> (after detection)\n";
                return;
            }
            QFile report(parts[2] + QStringLiteral(".txt"));
            QFile frame(parts[2] + QStringLiteral(".frame"));
            if (!report.open(QIODevice::WriteOnly | QIODevice::Truncate)
                || !frame.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
                m_out << "failed to save sadmac capture\n";
                return;
            }
            QTextStream stream(&report);
            reportSadMac(stream, maxDebugTraceEntries);
            stream.flush();
            frame.write(m_sadMac.frame.pixels);
            m_out << "saved " << report.fileName() << " and " << frame.fileName() << '\n';
        } else {
            m_out << "usage: sadmac arm|run [max-cycles]|status|report|save <prefix>|clear\n";
        }
    }

    void printPaths()
    {
        m_out << "config_root=" << cutemac::config::ConfigurationManager::configRootPath() << '\n';
        m_out << "profiles=" << cutemac::config::ConfigurationManager::profileDirectoryPath() << '\n';
        m_out << "roms=" << cutemac::config::ConfigurationManager::romDirectoryPath() << '\n';
        m_out << "disk_images=" << cutemac::config::ConfigurationManager::diskImageDirectoryPath() << '\n';
    }

    [[nodiscard]] bool requireRom()
    {
        if (m_romLoaded) {
            return true;
        }
        m_out << "ROM is not loaded\n";
        return false;
    }

    [[nodiscard]] QString displayPath(const QString& path) const
    {
        return path.isEmpty() ? QStringLiteral("(not set)") : QFileInfo(path).absoluteFilePath();
    }

    [[nodiscard]] std::optional<std::uint32_t> parseAddress(const QString& text) const
    {
        if (const auto numeric = parseNumber(text); numeric.has_value()) {
            return *numeric;
        }
        if (m_symbols.contains(text)) {
            return m_symbols[text];
        }
        if (m_lowMemoryNames.contains(text)) {
            return m_lowMemoryNames[text];
        }
        return std::nullopt;
    }

    [[nodiscard]] bool tracingRequiresStepping() const
    {
        return !m_breakpoints.empty() || m_trace.pc || m_trace.irq || m_trace.trap || m_trace.driver
            || m_trace.lowmem || m_trace.screen || m_trace.sound || m_trace.timeline;
    }

    int runCyclesWithTracing(int cycles)
    {
        int cyclesRun = 0;
        while (cyclesRun < cycles) {
            if (m_breakpoints.count(m_machine->programCounter()) != 0 && cyclesRun != 0) {
                break;
            }
            sampleBeforeStep();
            cyclesRun += std::max(1, m_machine->stepInstruction());
            sampleAfterStep();
        }
        return cyclesRun;
    }

    void sampleBeforeStep()
    {
        const auto pc = m_machine->programCounter();
        if (m_trace.pc) {
            appendRing(m_pcTrace, QStringLiteral("%1 %2").arg(hexValue(pc), symbolFor(pc)));
        }
        if (m_trace.trap) {
            const auto opcode = m_machine->debugRead16(pc);
            if ((opcode & 0xf000) == 0xa000) {
                appendRing(m_trapTrace, QStringLiteral("pc=%1 trap=0x%2").arg(hexValue(pc), QString::number(opcode, 16)));
                appendTimeline(QStringLiteral("trap pc=%1 opcode=0x%2").arg(hexValue(pc), QString::number(opcode, 16)));
            }
        }
        if (m_trace.driver) {
            if (m_sonyProbePcs.contains(pc)) {
                const auto regs = m_machine->cpuRegisters();
                QString event;
                if (pc == 0x0041811c) {
                    const auto dce = m_machine->debugRead32(regs.a[1]);
                    const auto request = m_machine->debugRead32(dce + 8);
                    event = QStringLiteral("%1 pc=%2 trap=%3 position=%4 request=%5 actual=%6 buffer=%7 dce=%8 pb=%9")
                                .arg(m_sonyProbePcs[pc], hexValue(pc), hexValue(m_machine->debugRead16(request + 6), 4),
                                    hexValue(m_machine->debugRead32(dce + 16)), hexValue(m_machine->debugRead32(request + 36)),
                                    hexValue(m_machine->debugRead32(request + 40)), hexValue(m_machine->debugRead32(request + 32)),
                                    hexValue(dce), hexValue(request));
                } else {
                    event = QStringLiteral("%1 pc=%2 d0=%3 d1=%4 d2=%5 a0=%6")
                                .arg(m_sonyProbePcs[pc], hexValue(pc), hexValue(regs.d[0]), hexValue(regs.d[1]), hexValue(regs.d[2]), hexValue(regs.a[0]));
                }
                appendRing(m_driverTrace, event);
                appendTimeline(QStringLiteral("driver ") + event);
            }
        }
    }

    void sampleAfterStep()
    {
        if (m_trace.irq) {
            const auto via = m_machine->viaDebugState();
            const auto active = via.interruptActive;
            if (!m_irqInitialized || active != m_lastIrqActive || via.interruptFlags != m_lastIrqFlags || via.interruptEnable != m_lastIrqEnable) {
                m_irqInitialized = true;
                m_lastIrqActive = active;
                m_lastIrqFlags = via.interruptFlags;
                m_lastIrqEnable = via.interruptEnable;
                const auto event = QStringLiteral("pc=%1 irq=%2 ifr=%3 ier=%4")
                    .arg(hexValue(m_machine->programCounter()), active ? QStringLiteral("on") : QStringLiteral("off"), hexValue(via.interruptFlags, 2), hexValue(via.interruptEnable, 2));
                appendRing(m_irqTrace, event);
                appendTimeline(QStringLiteral("irq ") + event);
            }
        }
        if (m_trace.lowmem) {
            for (auto it = m_lowMemoryWatchValues.begin(); it != m_lowMemoryWatchValues.end(); ++it) {
                const auto address = parseAddress(it.key());
                if (!address) {
                    continue;
                }
                const auto now = m_machine->debugRead32(*address);
                if (now != it.value()) {
                    const auto event = QStringLiteral("%1 %2 -> %3 pc=%4").arg(it.key(), hexValue(it.value()), hexValue(now), hexValue(m_machine->programCounter()));
                    it.value() = now;
                    appendTimeline(QStringLiteral("lowmem ") + event);
                }
            }
        }
    }

    void appendTimeline(const QString& event)
    {
        if (m_trace.timeline || m_trace.screen || m_trace.irq || m_trace.trap || m_trace.driver || m_trace.lowmem) {
            appendRing(m_timeline, event);
        }
    }

    static void appendRing(QStringList& ring, const QString& event)
    {
        if (ring.size() == maxDebugTraceEntries) {
            ring.removeFirst();
        }
        ring.append(event);
    }

    [[nodiscard]] QString symbolFor(std::uint32_t address) const
    {
        for (auto it = m_symbols.cbegin(); it != m_symbols.cend(); ++it) {
            if (it.value() == address) {
                return it.key();
            }
        }
        return m_sonyProbePcs.value(address, QString());
    }

    static const char* onOff(bool value)
    {
        return value ? "on" : "off";
    }

    cutemac::config::Configuration m_configuration;
    std::unique_ptr<cutemac::core::EmulationSession> m_session;
    cutemac::core::IDebugCpuAccess* m_cpuDebug = nullptr;
    cutemac::machines::macplus::MacPlusMachine* m_machine = nullptr;
    cutemac::machines::maciicx::MacIIcxMachine* m_iicxMachine = nullptr;
    cutemac::machines::powermac8100::PowerMac8100Machine* m_powerMac8100Machine = nullptr;
    std::unique_ptr<GdbStub> m_gdbStub;
    QTextStream m_out { stdout };
    bool m_romLoaded = false;
    bool m_gdbEnabled = false;
    std::int16_t m_debugMouseX = 0;
    std::int16_t m_debugMouseY = 0;
    bool m_debugMouseButton = false;
    quint16 m_gdbPort = 1234;
    std::set<std::uint32_t> m_breakpoints;
    QStringList m_watches;
    TraceOptions m_trace;
    QStringList m_pcTrace;
    QStringList m_irqTrace;
    QStringList m_trapTrace;
    QStringList m_driverTrace;
    QStringList m_timeline;
    SadMacCapture m_sadMac;
    QMap<QString, MemorySnapshot> m_memorySnapshots;
    QMap<QString, std::uint32_t> m_symbols {
        { QStringLiteral("ROMBootSpin"), 0x004007ba },
        { QStringLiteral("Sony_RdData"), 0x00402174 },
        { QStringLiteral("Sony_EjectOrSwitchDisk"), 0x0040016e },
    };
    QMap<std::uint32_t, QString> m_sonyProbePcs {
        { 0x004007ba, QStringLiteral("ROMBootSpin") },
        { 0x00402174, QStringLiteral("Sony_RdData") },
        { 0x0040016e, QStringLiteral("Sony_EjectOrSwitchDisk") },
        { 0x0041811c, QStringLiteral("Sony_DiskPrimeRequest") },
        { 0x0041813e, QStringLiteral("Sony_DiskPrimeParamErr") },
    };
    QMap<QString, std::uint32_t> m_lowMemoryNames {
        { QStringLiteral("MemTop"), 0x0108 },
        { QStringLiteral("BufPtr"), 0x010c },
        { QStringLiteral("DskErr"), 0x0142 },
        { QStringLiteral("BootDrive"), 0x0210 },
        { QStringLiteral("DSAlertTab"), 0x02ba },
        { QStringLiteral("TagData"), 0x02fa },
        { QStringLiteral("TagData_1"), 0x02fb },
        { QStringLiteral("BufTgFNum"), 0x02fc },
        { QStringLiteral("BufTgFFlg"), 0x0300 },
        { QStringLiteral("BufTgFBkNum"), 0x0302 },
        { QStringLiteral("BufTgDate"), 0x0304 },
        { QStringLiteral("BufTgHD20"), 0x038a },
        { QStringLiteral("BufTgHD20_1"), 0x038e },
        { QStringLiteral("Ticks"), 0x016a },
        { QStringLiteral("VIA"), 0x01d4 },
        { QStringLiteral("IWM"), 0x01e0 },
        { QStringLiteral("ResErr"), 0x0a60 },
    };
    QMap<QString, std::uint32_t> m_lowMemoryWatchValues;
    bool m_irqInitialized = false;
    bool m_lastIrqActive = false;
    std::uint8_t m_lastIrqFlags = 0;
    std::uint8_t m_lastIrqEnable = 0;
};

} // namespace

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("friedkiwi"));
    QCoreApplication::setApplicationName(QStringLiteral("CuteMac"));

    QCommandLineParser parser;
    parser.addHelpOption();
    const QCommandLineOption profileOption(QStringLiteral("profile"), QStringLiteral("Path to a CuteMac TOML profile."), QStringLiteral("path"));
    parser.addOption(profileOption);
    parser.process(app);

    cutemac::config::ConfigurationManager manager;
    auto configuration = cutemac::config::ConfigurationManager::defaultMacPlusConfiguration();
    if (parser.isSet(profileOption)) {
        const auto loaded = manager.loadTomlFile(parser.value(profileOption));
        if (loaded.has_value()) {
            configuration = *loaded;
        }
    }

    DebugConsole console(configuration);
    return console.run();
}
