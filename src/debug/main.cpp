#include <readline/history.h>
#include <readline/readline.h>

#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QMap>
#include <QSaveFile>
#include <QStringList>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTextStream>

#include <algorithm>
#include <cstdlib>
#include <memory>
#include <optional>
#include <set>

#include "cutemac/config/Configuration.h"
#include "cutemac/machines/macplus/MacPlusMachine.h"

namespace {

constexpr std::uint32_t macPlusScreenWidth = 512;
constexpr std::uint32_t macPlusScreenHeight = 342;

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
        } else if (command == QStringLiteral("state")) {
            printState();
        } else if (command == QStringLiteral("regs")) {
            printRegisters();
        } else if (command == QStringLiteral("disasm")) {
            disassemble(parts);
        } else if (command == QStringLiteral("mem")) {
            dumpMemory(parts);
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
        } else if (command == QStringLiteral("screen")) {
            handleScreen(parts);
        } else if (command == QStringLiteral("sound")) {
            handleSound(parts);
        } else if (command == QStringLiteral("rom")) {
            handleRom(parts);
        } else if (command == QStringLiteral("disk")) {
            handleDisk(parts);
        } else if (command == QStringLiteral("trace")) {
            configureTrace(parts);
        } else if (command == QStringLiteral("log")) {
            handleLog(parts);
        } else if (command == QStringLiteral("script")) {
            runScript(parts);
        } else if (command == QStringLiteral("gdb")) {
            configureGdb(parts);
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
        m_out << "  regs | state | devices [via|iwm|scc|scsi]\n";
        m_out << "  step [count] | run [cycles] | run-until <addr> [max-cycles]\n";
        m_out << "  disasm [addr|pc] [count] | mem <addr> [len]\n";
        m_out << "  write8|write16|write32 <addr> <value>\n";
        m_out << "  break <addr> | delete <addr|all> | breaks\n";
        m_out << "  watch read|write|rw <addr> [size]\n";
        m_out << "  bus [last [count]|clear|filter <region>]\n";
        m_out << "  vectors | globals | rom info\n";
        m_out << "  screen hash | screen export <file.png>\n";
        m_out << "  sound hash | sound export <file.wav> | sound capture-hash | sound capture-export <file.wav> | sound clear-capture\n";
        m_out << "  profile [set <key> <value>|save] | load <profile.toml>\n";
        m_out << "  disk insert <path> | disk eject\n";
        m_out << "  trace [category on|off] | log save <file.jsonl>\n";
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
        m_out << "ram_size_mib=" << m_configuration.ramSizeMiB << '\n';
        m_out << "cycles_per_frame=" << m_configuration.cyclesPerFrame << '\n';
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
        } else if (key == QStringLiteral("cycles_per_frame")) {
            m_configuration.cyclesPerFrame = value.toInt();
        } else if (key == QStringLiteral("ram_size_mib")) {
            m_configuration.ramSizeMiB = value.toInt();
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
        m_machine = std::make_unique<cutemac::machines::macplus::MacPlusMachine>(
            static_cast<std::size_t>(std::max(1, m_configuration.ramSizeMiB)) * 1024 * 1024);
        m_gdbStub = std::make_unique<GdbStub>(*m_machine, m_breakpoints);
        m_gdbStub->setEnabled(m_gdbEnabled);
        m_gdbStub->setPort(m_gdbPort);

        m_romLoaded = !m_configuration.romPath.isEmpty() && m_machine->loadRomFile(m_configuration.romPath);
        if (m_romLoaded) {
            m_machine->reset();
            m_out << "machine reset: pc=" << hexValue(m_machine->programCounter()) << '\n';
        } else {
            m_out << "machine reset without ROM; set rom_path in profile\n";
        }
    }

    void runCycles(const QStringList& parts)
    {
        if (!requireRom()) {
            return;
        }
        const auto cycles = parts.size() >= 2 ? parts[1].toInt() : m_configuration.cyclesPerFrame;
        const auto cyclesRun = m_machine->runCycles(cycles);
        m_out << "cycles_run=" << cyclesRun << " pc=" << hexValue(m_machine->programCounter()) << '\n';
    }

    void stepInstructions(const QStringList& parts)
    {
        if (!requireRom()) {
            return;
        }
        const auto count = parts.size() >= 2 ? std::max(1, parts[1].toInt()) : 1;
        for (int i = 0; i < count; ++i) {
            if (m_breakpoints.count(m_machine->programCounter()) != 0 && i != 0) {
                break;
            }
            (void)m_machine->stepInstruction();
        }
        m_out << "pc=" << hexValue(m_machine->programCounter()) << ' ' << m_machine->disassemble(m_machine->programCounter()) << '\n';
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
        const auto hit = m_machine->runUntilPc(*address, maxCycles);
        m_out << (hit ? "hit " : "timeout ") << hexValue(m_machine->programCounter()) << '\n';
    }

    void printState()
    {
        const auto& summary = m_machine->accessSummary();
        m_out << "pc=" << hexValue(m_machine->programCounter()) << '\n';
        m_out << "overlay=" << (m_machine->overlayEnabled() ? "on" : "off") << '\n';
        m_out << "ram_reads=" << summary.ramReads << " ram_writes=" << summary.ramWrites << '\n';
        m_out << "rom_reads=" << summary.romReads << '\n';
        m_out << "configuration_reads=" << summary.configurationReads << '\n';
        m_out << "synthetic_tick_reads=" << summary.syntheticTickReads << '\n';
        m_out << "unmapped_reads=" << summary.unmappedReads << " unmapped_writes=" << summary.unmappedWrites << '\n';
    }

    void printRegisters()
    {
        const auto regs = m_machine->cpuRegisters();
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
        auto address = m_machine->programCounter();
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
            const auto text = m_machine->disassemble(address);
            m_out << hexValue(address) << "  " << text << '\n';
            address += static_cast<std::uint32_t>(std::max(2, m_machine->disassembleBytes(address)));
        }
    }

    void dumpMemory(const QStringList& parts)
    {
        if (parts.size() < 2) {
            m_out << "usage: mem <addr> [len]\n";
            return;
        }
        const auto address = parseNumber(parts[1]);
        if (!address.has_value()) {
            m_out << "invalid address\n";
            return;
        }
        const auto length = parts.size() >= 3 ? std::max(1U, *parseNumber(parts[2])) : 128U;
        for (std::uint32_t offset = 0; offset < length; offset += 16) {
            m_out << hexValue(*address + offset) << " ";
            for (std::uint32_t i = 0; i < 16 && offset + i < length; ++i) {
                m_out << byteToHex(m_machine->debugRead8(*address + offset + i)) << ' ';
            }
            m_out << '\n';
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
        const auto& summary = m_machine->accessSummary();
        const auto device = parts.size() >= 2 ? parts[1].toLower() : QString();
        if (device.isEmpty() || device == QStringLiteral("via")) {
            m_out << "via_reads=" << summary.viaReads << " via_writes=" << summary.viaWrites << '\n';
        }
        if (device.isEmpty() || device == QStringLiteral("scc")) {
            m_out << "scc_reads=" << summary.sccReads << " scc_writes=" << summary.sccWrites << '\n';
        }
        if (device.isEmpty() || device == QStringLiteral("iwm")) {
            m_out << "iwm_reads=" << summary.iwmReads << " iwm_writes=" << summary.iwmWrites << '\n';
        }
        if (device.isEmpty() || device == QStringLiteral("scsi")) {
            m_out << "scsi_reads=" << summary.scsiReads << " scsi_writes=" << summary.scsiWrites << '\n';
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
        if (parts.size() >= 2 && parts[1] == QStringLiteral("hash")) {
            m_out << "screen_hash=" << hexValue(m_machine->framebufferHash()) << '\n';
            return;
        }
        if (parts.size() >= 3 && parts[1] == QStringLiteral("export")) {
            exportScreen(parts[2]);
            return;
        }
        m_out << "usage: screen hash | screen export <file.png>\n";
    }

    void exportScreen(const QString& path)
    {
        const auto bytes = m_machine->framebufferBytes();
        QImage image(macPlusScreenWidth, macPlusScreenHeight, QImage::Format_RGB32);
        for (int y = 0; y < image.height(); ++y) {
            for (int x = 0; x < image.width(); ++x) {
                const auto byte = static_cast<std::uint8_t>(bytes[(y * image.width() + x) / 8]);
                const auto bit = 7 - (x & 7);
                const auto on = ((byte >> bit) & 1) != 0;
                image.setPixelColor(x, y, on ? Qt::black : Qt::white);
            }
        }
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
    }

    void handleDisk(const QStringList& parts)
    {
        if (parts.size() >= 3 && parts[1] == QStringLiteral("insert")) {
            m_configuration.diskPath = parts[2];
            m_out << "disk inserted: " << displayPath(m_configuration.diskPath) << '\n';
        } else if (parts.size() >= 2 && parts[1] == QStringLiteral("eject")) {
            m_configuration.diskPath.clear();
            m_out << "disk ejected\n";
        } else {
            m_out << "usage: disk insert <path> | disk eject\n";
        }
    }

    void configureTrace(const QStringList& parts)
    {
        if (parts.size() == 1) {
            m_out << "trace categories:";
            for (auto it = m_traceCategories.cbegin(); it != m_traceCategories.cend(); ++it) {
                m_out << ' ' << it.key() << '=' << (it.value() ? "on" : "off");
            }
            m_out << '\n';
            return;
        }
        if (parts.size() != 3) {
            m_out << "usage: trace [category on|off]\n";
            return;
        }
        const auto enabled = parts[2].compare(QStringLiteral("on"), Qt::CaseInsensitive) == 0;
        m_traceCategories[parts[1].toLower()] = enabled;
        m_out << "trace " << parts[1].toLower() << '=' << (enabled ? "on" : "off") << '\n';
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

    cutemac::config::Configuration m_configuration;
    std::unique_ptr<cutemac::machines::macplus::MacPlusMachine> m_machine;
    std::unique_ptr<GdbStub> m_gdbStub;
    QTextStream m_out { stdout };
    bool m_romLoaded = false;
    bool m_gdbEnabled = false;
    quint16 m_gdbPort = 1234;
    std::set<std::uint32_t> m_breakpoints;
    QStringList m_watches;
    QMap<QString, bool> m_traceCategories {
        { QStringLiteral("bus"), false },
        { QStringLiteral("cpu"), false },
        { QStringLiteral("devices"), false },
        { QStringLiteral("video"), false },
    };
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
