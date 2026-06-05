#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace fs = std::filesystem;

constexpr const char* kVersion = "1.0.0";

struct Config {
    int warn = 20;
    int critical = 10;
    int suspend = 5;
    int poll_interval_ms = 3000;
    int lock_delay_ms = 1500;
    bool once = false;
    bool dry_run = false;
    bool print_detection = false;
    bool ignore_charging = true;
    bool charger_notifications = true;
    fs::path power_supply_path = "/sys/class/power_supply";
    std::optional<fs::path> battery_path;
    std::string warn_command = "notify-send -a battery -i battery-low 'Low battery' 'Battery is at {percent}%'";
    std::string critical_command = "notify-send -u critical -a battery -i battery-empty 'Critical battery' 'Battery is at {percent}%'";
    std::string charging_command = "notify-send -a battery -i battery-good-charging 'Power connected' 'Charging started'";
    std::string discharging_command = "notify-send -a battery -i battery-good 'Power disconnected' 'Running on battery'";
    std::string full_command = "notify-send -a battery -i battery-full 'Battery full' 'Charging complete'";
    std::string lock_command = "loginctl lock-session";
    std::string suspend_command = "systemctl suspend";
};

struct BatteryReading {
    fs::path path;
    std::string name;
    std::string status = "Unknown";
    int percent = 0;
    std::string source;
};

struct DetectionResult {
    std::vector<BatteryReading> readings;
    std::optional<BatteryReading> selected;
};

struct State {
    bool warn_sent = false;
    bool critical_sent = false;
    bool suspend_sent = false;
    std::optional<std::string> last_status;
};

std::string trim(std::string value)
{
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) {
        return "";
    }
    const auto last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1);
}

std::optional<std::string> read_text(const fs::path& path)
{
    std::ifstream file(path);
    if (!file) {
        return std::nullopt;
    }

    std::ostringstream buffer;
    buffer << file.rdbuf();
    return trim(buffer.str());
}

std::optional<long long> read_int(const fs::path& path)
{
    const auto text = read_text(path);
    if (!text || text->empty()) {
        return std::nullopt;
    }

    try {
        size_t index = 0;
        const auto value = std::stoll(*text, &index, 10);
        if (index != text->size()) {
            return std::nullopt;
        }
        return value;
    } catch (...) {
        return std::nullopt;
    }
}

std::string replace_all(std::string value, const std::string& needle, const std::string& replacement)
{
    size_t pos = 0;
    while ((pos = value.find(needle, pos)) != std::string::npos) {
        value.replace(pos, needle.size(), replacement);
        pos += replacement.size();
    }
    return value;
}

std::string render_command(std::string command, const BatteryReading& battery)
{
    command = replace_all(command, "{percent}", std::to_string(battery.percent));
    command = replace_all(command, "{status}", battery.status);
    command = replace_all(command, "{battery}", battery.name);
    return command;
}

bool is_discharge_status(const std::string& status)
{
    return status == "Discharging";
}

bool is_battery(const fs::path& path)
{
    const auto type = read_text(path / "type");
    return type && *type == "Battery";
}

std::optional<int> percent_from_ratio(const fs::path& path, const std::string& now_name, const std::string& full_name)
{
    const auto now = read_int(path / now_name);
    const auto full = read_int(path / full_name);
    if (!now || !full || *now < 0 || *full <= 0) {
        return std::nullopt;
    }

    const auto percent = static_cast<int>(std::lround((static_cast<double>(*now) * 100.0) / static_cast<double>(*full)));
    return std::clamp(percent, 0, 100);
}

std::optional<std::pair<int, std::string>> detect_percent(const fs::path& path)
{
    if (const auto capacity = read_int(path / "capacity")) {
        if (*capacity >= 0) {
            return std::pair { std::clamp(static_cast<int>(*capacity), 0, 100), std::string("capacity") };
        }
    }

    if (const auto percent = percent_from_ratio(path, "energy_now", "energy_full")) {
        return std::pair { *percent, std::string("energy_now/energy_full") };
    }

    if (const auto percent = percent_from_ratio(path, "charge_now", "charge_full")) {
        return std::pair { *percent, std::string("charge_now/charge_full") };
    }

    return std::nullopt;
}

std::optional<BatteryReading> read_battery(const fs::path& path, bool require_type_file)
{
    if (require_type_file && !is_battery(path)) {
        return std::nullopt;
    }

    const auto percent = detect_percent(path);
    if (!percent) {
        return std::nullopt;
    }

    BatteryReading reading;
    reading.path = path;
    reading.name = path.filename().string();
    reading.status = read_text(path / "status").value_or("Unknown");
    reading.percent = percent->first;
    reading.source = percent->second;
    return reading;
}

DetectionResult detect_battery(const Config& config)
{
    DetectionResult result;

    if (config.battery_path) {
        if (const auto reading = read_battery(*config.battery_path, false)) {
            result.readings.push_back(*reading);
            if (!config.ignore_charging || is_discharge_status(reading->status)) {
                result.selected = *reading;
            }
        }
        return result;
    }

    std::error_code error;
    if (!fs::is_directory(config.power_supply_path, error)) {
        return result;
    }

    for (const auto& entry : fs::directory_iterator(config.power_supply_path, error)) {
        if (error) {
            break;
        }
        if (!entry.is_directory(error) && !entry.is_symlink()) {
            continue;
        }
        if (const auto reading = read_battery(entry.path(), true)) {
            result.readings.push_back(*reading);
        }
    }

    for (const auto& reading : result.readings) {
        if (config.ignore_charging && !is_discharge_status(reading.status)) {
            continue;
        }
        if (!result.selected || reading.percent < result.selected->percent) {
            result.selected = reading;
        }
    }

    return result;
}

std::optional<BatteryReading> status_battery(const DetectionResult& detection)
{
    if (detection.selected) {
        return detection.selected;
    }
    if (detection.readings.empty()) {
        return std::nullopt;
    }

    return *std::min_element(detection.readings.begin(), detection.readings.end(), [](const BatteryReading& left, const BatteryReading& right) {
        return left.percent < right.percent;
    });
}

int run_command(const Config& config, const std::string& label, const std::string& command)
{
    if (command.empty()) {
        return 0;
    }

    if (config.dry_run) {
        std::cout << "Would run " << label << ": " << command << '\n';
        return 0;
    }

    std::cout << "Running " << label << ": " << command << '\n';
    return std::system(command.c_str());
}

void wait_ms(const Config& config, int milliseconds)
{
    if (milliseconds <= 0) {
        return;
    }
    if (config.dry_run) {
        std::cout << "Would wait " << milliseconds << "ms\n";
        return;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(milliseconds));
}

void print_detection(const DetectionResult& detection)
{
    if (detection.readings.empty()) {
        std::cout << "No batteries detected\n";
    }

    for (const auto& reading : detection.readings) {
        std::cout << "Battery " << reading.name << ": " << reading.percent << "%, " << reading.status
                  << " (" << reading.source << ", " << reading.path << ")\n";
    }

    if (detection.selected) {
        std::cout << "Selected " << detection.selected->name << ": " << detection.selected->percent << "%, "
                  << detection.selected->status << '\n';
    } else {
        std::cout << "No discharging battery selected\n";
    }
}

void handle_reading(const Config& config, State& state, const BatteryReading& battery)
{
    std::cout << "Battery " << battery.name << ": " << battery.percent << "%, " << battery.status << '\n';

    if (battery.percent > config.warn) {
        state.warn_sent = false;
        state.critical_sent = false;
        state.suspend_sent = false;
        return;
    }

    if (battery.percent > config.critical) {
        state.critical_sent = false;
        state.suspend_sent = false;
        if (!state.warn_sent) {
            run_command(config, "warn command", render_command(config.warn_command, battery));
            state.warn_sent = true;
        }
        return;
    }

    if (battery.percent > config.suspend) {
        state.suspend_sent = false;
        if (!state.critical_sent) {
            run_command(config, "critical command", render_command(config.critical_command, battery));
            state.critical_sent = true;
        }
        return;
    }

    if (!state.suspend_sent) {
        state.suspend_sent = true;
        run_command(config, "lock command", config.lock_command);
        wait_ms(config, config.lock_delay_ms);
        run_command(config, "suspend command", config.suspend_command);
    }
}

void handle_status_change(const Config& config, State& state, const BatteryReading& battery)
{
    if (!config.charger_notifications) {
        return;
    }

    if (!state.last_status) {
        state.last_status = battery.status;
        return;
    }

    if (*state.last_status == battery.status) {
        return;
    }

    state.last_status = battery.status;

    if (battery.status == "Charging") {
        state.warn_sent = false;
        state.critical_sent = false;
        state.suspend_sent = false;
        if (battery.percent < 97) {
            run_command(config, "charging command", render_command(config.charging_command, battery));
        }
    } else if (battery.status == "Discharging") {
        run_command(config, "discharging command", render_command(config.discharging_command, battery));
    } else if (battery.status == "Full") {
        state.warn_sent = false;
        state.critical_sent = false;
        state.suspend_sent = false;
        run_command(config, "full command", render_command(config.full_command, battery));
    }
}

std::optional<int> parse_int_arg(const std::string& name, const std::string& value)
{
    try {
        size_t index = 0;
        const auto parsed = std::stoi(value, &index, 10);
        if (index != value.size()) {
            std::cerr << name << " expects an integer\n";
            return std::nullopt;
        }
        return parsed;
    } catch (...) {
        std::cerr << name << " expects an integer\n";
        return std::nullopt;
    }
}

void print_usage(const char* argv0)
{
    std::cout << "Usage: " << argv0 << " [options]\n"
              << "\n"
              << "Options:\n"
              << "  -w, --warn N                  Low battery warning threshold (default 20)\n"
              << "  -c, --critical N              Critical battery warning threshold (default 10)\n"
              << "  -s, --suspend N               Suspend threshold (default 5)\n"
              << "  -b, --battery-path PATH       Read one battery directory instead of scanning sysfs\n"
              << "  -p, --power-supply-path PATH  Scan another power_supply directory\n"
              << "  -o, --once                    Poll once and exit\n"
              << "  -n, --dry-run                 Print commands instead of executing them\n"
              << "  -i, --poll-interval-ms N      Poll interval in milliseconds (default 30000)\n"
              << "  -l, --lock-delay-ms N         Delay between lock and suspend (default 1500)\n"
              << "  -W, --warn-command CMD        Command for warn notification\n"
              << "  -C, --critical-command CMD    Command for critical notification\n"
              << "  --charging-command CMD        Command for charging notification\n"
              << "  --discharging-command CMD     Command for discharging notification\n"
              << "  --full-command CMD            Command for full notification\n"
              << "  -L, --lock-command CMD        Command to lock before suspend\n"
              << "  -S, --suspend-command CMD     Command to suspend\n"
              << "  -d, --print-detection         Print discovered battery details\n"
              << "  -I, --include-charging        Allow warnings/suspend even if not discharging\n"
              << "  --no-charger-notifications    Disable charging status change notifications\n"
              << "  -V, --version                 Show version\n"
              << "  -h, --help                    Show this help\n";
}

std::optional<Config> parse_args(int argc, char** argv)
{
    Config config;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];

        auto require_value = [&](const std::string& option) -> std::optional<std::string> {
            if (i + 1 >= argc) {
                std::cerr << option << " requires a value\n";
                return std::nullopt;
            }
            return std::string(argv[++i]);
        };

        auto parse_int_option = [&](const std::string& option) -> std::optional<int> {
            const auto value = require_value(option);
            if (!value) {
                return std::nullopt;
            }
            return parse_int_arg(option, *value);
        };

        if (arg == "-h" || arg == "--help") {
            print_usage(argv[0]);
            std::exit(0);
        } else if (arg == "-V" || arg == "--version") {
            std::cout << kVersion << '\n';
            std::exit(0);
        } else if (arg == "-w" || arg == "--warn") {
            const auto value = parse_int_option(arg);
            if (!value) return std::nullopt;
            config.warn = *value;
        } else if (arg == "-c" || arg == "--critical") {
            const auto value = parse_int_option(arg);
            if (!value) return std::nullopt;
            config.critical = *value;
        } else if (arg == "-s" || arg == "--suspend") {
            const auto value = parse_int_option(arg);
            if (!value) return std::nullopt;
            config.suspend = *value;
        } else if (arg == "-i" || arg == "--poll-interval-ms") {
            const auto value = parse_int_option(arg);
            if (!value) return std::nullopt;
            config.poll_interval_ms = *value;
        } else if (arg == "-l" || arg == "--lock-delay-ms") {
            const auto value = parse_int_option(arg);
            if (!value) return std::nullopt;
            config.lock_delay_ms = *value;
        } else if (arg == "-b" || arg == "--battery-path") {
            const auto value = require_value(arg);
            if (!value) return std::nullopt;
            config.battery_path = *value;
        } else if (arg == "-p" || arg == "--power-supply-path") {
            const auto value = require_value(arg);
            if (!value) return std::nullopt;
            config.power_supply_path = *value;
        } else if (arg == "-W" || arg == "--warn-command") {
            const auto value = require_value(arg);
            if (!value) return std::nullopt;
            config.warn_command = *value;
        } else if (arg == "-C" || arg == "--critical-command") {
            const auto value = require_value(arg);
            if (!value) return std::nullopt;
            config.critical_command = *value;
        } else if (arg == "--charging-command") {
            const auto value = require_value(arg);
            if (!value) return std::nullopt;
            config.charging_command = *value;
        } else if (arg == "--discharging-command") {
            const auto value = require_value(arg);
            if (!value) return std::nullopt;
            config.discharging_command = *value;
        } else if (arg == "--full-command") {
            const auto value = require_value(arg);
            if (!value) return std::nullopt;
            config.full_command = *value;
        } else if (arg == "-L" || arg == "--lock-command") {
            const auto value = require_value(arg);
            if (!value) return std::nullopt;
            config.lock_command = *value;
        } else if (arg == "-S" || arg == "--suspend-command") {
            const auto value = require_value(arg);
            if (!value) return std::nullopt;
            config.suspend_command = *value;
        } else if (arg == "-o" || arg == "--once") {
            config.once = true;
        } else if (arg == "-n" || arg == "--dry-run") {
            config.dry_run = true;
        } else if (arg == "-d" || arg == "--print-detection") {
            config.print_detection = true;
        } else if (arg == "-I" || arg == "--include-charging") {
            config.ignore_charging = false;
        } else if (arg == "--no-charger-notifications") {
            config.charger_notifications = false;
        } else {
            std::cerr << "Unknown option: " << arg << '\n';
            return std::nullopt;
        }
    }

    if (config.warn < 0 || config.warn > 100 || config.critical < 0 || config.critical > 100 || config.suspend < 0 || config.suspend > 100) {
        std::cerr << "Thresholds must be between 0 and 100\n";
        return std::nullopt;
    }
    if (!(config.warn >= config.critical && config.critical >= config.suspend)) {
        std::cerr << "Thresholds must satisfy: --warn >= --critical >= --suspend\n";
        return std::nullopt;
    }
    if (config.poll_interval_ms <= 0) {
        std::cerr << "--poll-interval-ms must be positive\n";
        return std::nullopt;
    }
    if (config.lock_delay_ms < 0) {
        std::cerr << "--lock-delay-ms must be zero or positive\n";
        return std::nullopt;
    }

    return config;
}

int main(int argc, char** argv)
{
    const auto config = parse_args(argc, argv);
    if (!config) {
        print_usage(argv[0]);
        return 2;
    }

    State state;

    while (true) {
        const auto detection = detect_battery(*config);

        if (config->print_detection) {
            print_detection(detection);
        }

        if (const auto battery = status_battery(detection)) {
            handle_status_change(*config, state, *battery);
        }

        if (detection.selected) {
            handle_reading(*config, state, *detection.selected);
        } else if (!config->print_detection) {
            std::cout << "No discharging battery selected\n";
        }

        if (config->once) {
            break;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(config->poll_interval_ms));
    }

    return 0;
}
