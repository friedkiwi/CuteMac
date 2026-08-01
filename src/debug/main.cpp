#include <readline/history.h>
#include <readline/readline.h>

#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QFileInfo>
#include <QStringList>
#include <QTextStream>

#include <cstdlib>
#include <memory>
#include <optional>

#include "cutemac/config/Configuration.h"
#include "cutemac/machines/macplus/MacPlusMachine.h"

namespace {

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
            printProfile();
        } else if (command == QStringLiteral("load")) {
            loadProfile(parts);
        } else if (command == QStringLiteral("reset")) {
            reloadMachine();
        } else if (command == QStringLiteral("run")) {
            runCycles(parts);
        } else if (command == QStringLiteral("state")) {
            printState();
        } else if (command == QStringLiteral("devices")) {
            printDevices();
        } else if (command == QStringLiteral("trace")) {
            configureTrace(parts);
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
        m_out << "  help                         show commands\n";
        m_out << "  profile                      show active profile\n";
        m_out << "  load <profile.toml>          load another profile\n";
        m_out << "  reset                        reload ROM and reset machine\n";
        m_out << "  run [cycles]                 run emulation cycles\n";
        m_out << "  state                        show CPU and bus summary\n";
        m_out << "  devices                      show decoded device summary\n";
        m_out << "  trace [category on|off]      show or set trace categories\n";
        m_out << "  gdb [enable|disable|port N]  configure future GDB stub settings\n";
        m_out << "  paths                        show profile/ROM/disk folders\n";
        m_out << "  quit                         exit\n";
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

        m_romLoaded = !m_configuration.romPath.isEmpty() && m_machine->loadRomFile(m_configuration.romPath);
        if (m_romLoaded) {
            m_machine->reset();
            m_out << "machine reset: pc=0x" << QString::number(m_machine->programCounter(), 16) << '\n';
        } else {
            m_out << "machine reset without ROM; set rom_path in profile\n";
        }
    }

    void runCycles(const QStringList& parts)
    {
        if (!m_romLoaded) {
            m_out << "ROM is not loaded\n";
            return;
        }

        const auto cycles = parts.size() >= 2 ? parts[1].toInt() : m_configuration.cyclesPerFrame;
        const auto cyclesRun = m_machine->runCycles(cycles);
        m_out << "cycles_run=" << cyclesRun << " pc=0x" << QString::number(m_machine->programCounter(), 16) << '\n';
    }

    void printState()
    {
        const auto& summary = m_machine->accessSummary();
        m_out << "pc=0x" << QString::number(m_machine->programCounter(), 16) << '\n';
        m_out << "overlay=" << (m_machine->overlayEnabled() ? "on" : "off") << '\n';
        m_out << "ram_reads=" << summary.ramReads << " ram_writes=" << summary.ramWrites << '\n';
        m_out << "rom_reads=" << summary.romReads << '\n';
        m_out << "configuration_reads=" << summary.configurationReads << '\n';
        m_out << "synthetic_tick_reads=" << summary.syntheticTickReads << '\n';
        m_out << "unmapped_reads=" << summary.unmappedReads << " unmapped_writes=" << summary.unmappedWrites << '\n';
    }

    void printDevices()
    {
        const auto& summary = m_machine->accessSummary();
        m_out << "via_reads=" << summary.viaReads << " via_writes=" << summary.viaWrites << '\n';
        m_out << "scc_reads=" << summary.sccReads << " scc_writes=" << summary.sccWrites << '\n';
        m_out << "iwm_reads=" << summary.iwmReads << " iwm_writes=" << summary.iwmWrites << '\n';
        m_out << "scsi_reads=" << summary.scsiReads << " scsi_writes=" << summary.scsiWrites << '\n';
        for (const auto& event : m_machine->eventLog()) {
            m_out << "event: " << event << '\n';
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

    void configureGdb(const QStringList& parts)
    {
        if (parts.size() == 1) {
            printGdb();
            return;
        }

        const auto subcommand = parts[1].toLower();
        if (subcommand == QStringLiteral("enable")) {
            m_gdbEnabled = true;
        } else if (subcommand == QStringLiteral("disable")) {
            m_gdbEnabled = false;
        } else if (subcommand == QStringLiteral("port") && parts.size() >= 3) {
            bool ok = false;
            const auto port = parts[2].toUShort(&ok);
            if (ok) {
                m_gdbPort = port;
            } else {
                m_out << "invalid port\n";
            }
        } else {
            m_out << "usage: gdb [enable|disable|port N]\n";
            return;
        }

        printGdb();
    }

    void printGdb()
    {
        m_out << "gdb_stub=" << (m_gdbEnabled ? "enabled" : "disabled") << " port=" << m_gdbPort << '\n';
        m_out << "stub implementation pending; use gdb-multiarch with 'set architecture m68k:68000' once available\n";
    }

    void printPaths()
    {
        m_out << "config_root=" << cutemac::config::ConfigurationManager::configRootPath() << '\n';
        m_out << "profiles=" << cutemac::config::ConfigurationManager::profileDirectoryPath() << '\n';
        m_out << "roms=" << cutemac::config::ConfigurationManager::romDirectoryPath() << '\n';
        m_out << "disk_images=" << cutemac::config::ConfigurationManager::diskImageDirectoryPath() << '\n';
    }

    [[nodiscard]] QString displayPath(const QString& path) const
    {
        return path.isEmpty() ? QStringLiteral("(not set)") : QFileInfo(path).absoluteFilePath();
    }

    cutemac::config::Configuration m_configuration;
    std::unique_ptr<cutemac::machines::macplus::MacPlusMachine> m_machine;
    QTextStream m_out { stdout };
    bool m_romLoaded = false;
    bool m_gdbEnabled = false;
    quint16 m_gdbPort = 1234;
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
