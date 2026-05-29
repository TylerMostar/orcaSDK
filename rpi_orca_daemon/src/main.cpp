//copyright (c) 2026 Mostar Labs Inc. All rights reserved.

#include "actuator.h"

#include <atomic>
#include <chrono>
#include <csignal>
#include <cctype>
#include <cstdint>
#include <iostream>
#include <sstream>
#include <algorithm>
#include <string>
#include <thread>
#include <vector>

namespace {

std::atomic<bool> g_should_run{true};

void signal_handler(int)
{
    g_should_run.store(false);
}

struct MotorConfig {
    std::string device;
    uint8_t modbus_address = 1;
};

struct AppConfig {
    std::vector<MotorConfig> motors;
    int baud_rate = orcaSDK::Constants::kDefaultBaudRate;
    int interframe_delay_us = orcaSDK::Constants::kDefaultInterframeDelay_uS;
    uint16_t damping = 600;
    int open_retry_ms = 200;
    int configure_retry_ms = 100;
    int health_check_ms = 500;
    std::string serial_log_dir;
};

constexpr uint16_t kDamperEffectBit = (1u << 4);

enum class MotorRunState {
    WaitingForSerial,
    WaitingForHaptics,
    HapticsActive
};

const char* motor_state_to_string(MotorRunState state)
{
    switch (state) {
    case MotorRunState::WaitingForSerial: return "WAITING_FOR_SERIAL";
    case MotorRunState::WaitingForHaptics: return "WAITING_FOR_HAPTICS";
    case MotorRunState::HapticsActive: return "HAPTICS_ACTIVE";
    default: return "UNKNOWN";
    }
}

void log_line(const std::string& message)
{
    std::cout << message << std::endl;
}

void print_usage(const char* exe_name)
{
    std::cerr
        << "Usage:\n"
        << "  " << exe_name << " --device <path> [--device <path> ...] [options]\n\n"
        << "Required:\n"
        << "  --device <path>                 Linux device node (e.g., /dev/rs232_to_usb_converter_1)\n\n"
        << "Options:\n"
        << "  --baud <int>                    Default: 19200\n"
        << "  --interframe-us <int>           Default: 2000\n"
        << "  --damping <int>                 Default: 600\n"
        << "  --open-retry-ms <int>           Default: 200\n"
        << "  --configure-retry-ms <int>      Default: 100\n"
        << "  --health-check-ms <int>         Default: 500\n"
        << "  --address <int>                 Apply one Modbus address to all devices (default: 1)\n"
        << "  --device-address <path:addr>    Per-device Modbus address override\n"
        << "  --serial-log-dir <path>         Optional raw Modbus tx/rx log directory\n"
        << std::endl;
}

std::string lowercase(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

bool should_reopen_port(const orcaSDK::OrcaError& error)
{
    const std::string msg = lowercase(error.what());
    return msg.find("no opened serial port") != std::string::npos
        || msg.find("serial port not open") != std::string::npos
        || msg.find("permission") != std::string::npos
        || msg.find("no such file") != std::string::npos;
}

bool parse_path_address(const std::string& input, MotorConfig& config)
{
    const auto separator = input.find(':');
    if (separator == std::string::npos || separator == 0 || separator + 1 >= input.size()) {
        return false;
    }

    config.device = input.substr(0, separator);

    try {
        const int parsed_address = std::stoi(input.substr(separator + 1));
        if (parsed_address < 1 || parsed_address > 247) {
            return false;
        }
        config.modbus_address = static_cast<uint8_t>(parsed_address);
    }
    catch (...) {
        return false;
    }

    return true;
}

std::string sanitize_log_component(std::string value)
{
    for (char& c : value) {
        const auto uc = static_cast<unsigned char>(c);
        if (!std::isalnum(uc) && c != '-' && c != '_') {
            c = '-';
        }
    }
    return value;
}

std::string make_serial_log_path(const std::string& serial_log_dir, const MotorConfig& motor_config)
{
    if (serial_log_dir.empty()) {
        return "";
    }

    std::string dir = serial_log_dir;
    const char last = dir.back();
    if (last != '/' && last != '\\') {
        dir += '/';
    }

    return dir
        + sanitize_log_component(motor_config.device)
        + "-addr-"
        + std::to_string(motor_config.modbus_address)
        + ".tsv";
}

orcaSDK::OrcaError add_config_step_context(const char* step, const orcaSDK::OrcaError& error)
{
    if (!error) {
        return {false, ""};
    }

    return {true, std::string(step) + " failed: " + error.what()};
}

bool parse_args(int argc, char** argv, AppConfig& config)
{
    int global_address = 1;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];

        auto require_next = [&](const char* option) -> const char* {
            if (i + 1 >= argc) {
                std::cerr << "Missing value for " << option << std::endl;
                return nullptr;
            }
            return argv[++i];
        };

        if (arg == "--device") {
            const char* value = require_next("--device");
            if (!value) return false;
            config.motors.push_back({value, static_cast<uint8_t>(global_address)});
        }
        else if (arg == "--baud") {
            const char* value = require_next("--baud");
            if (!value) return false;
            config.baud_rate = std::stoi(value);
        }
        else if (arg == "--interframe-us") {
            const char* value = require_next("--interframe-us");
            if (!value) return false;
            config.interframe_delay_us = std::stoi(value);
        }
        else if (arg == "--damping") {
            const char* value = require_next("--damping");
            if (!value) return false;
            config.damping = static_cast<uint16_t>(std::stoi(value));
        }
        else if (arg == "--open-retry-ms") {
            const char* value = require_next("--open-retry-ms");
            if (!value) return false;
            config.open_retry_ms = std::stoi(value);
        }
        else if (arg == "--configure-retry-ms") {
            const char* value = require_next("--configure-retry-ms");
            if (!value) return false;
            config.configure_retry_ms = std::stoi(value);
        }
        else if (arg == "--health-check-ms") {
            const char* value = require_next("--health-check-ms");
            if (!value) return false;
            config.health_check_ms = std::stoi(value);
        }
        else if (arg == "--address") {
            const char* value = require_next("--address");
            if (!value) return false;
            global_address = std::stoi(value);
            if (global_address < 1 || global_address > 247) {
                std::cerr << "Invalid --address. Expected [1, 247]." << std::endl;
                return false;
            }
            for (auto& motor : config.motors) {
                motor.modbus_address = static_cast<uint8_t>(global_address);
            }
        }
        else if (arg == "--device-address") {
            const char* value = require_next("--device-address");
            if (!value) return false;
            MotorConfig cfg;
            if (!parse_path_address(value, cfg)) {
                std::cerr << "Invalid --device-address format. Use /dev/orca_name:addr" << std::endl;
                return false;
            }
            config.motors.push_back(cfg);
        }
        else if (arg == "--serial-log-dir") {
            const char* value = require_next("--serial-log-dir");
            if (!value) return false;
            config.serial_log_dir = value;
        }
        else if (arg == "--help" || arg == "-h") {
            return false;
        }
        else {
            std::cerr << "Unknown argument: " << arg << std::endl;
            return false;
        }
    }

    if (config.motors.empty()) {
        std::cerr << "At least one --device or --device-address is required." << std::endl;
        return false;
    }

    if (config.open_retry_ms < 10 || config.configure_retry_ms < 10 || config.health_check_ms < 50) {
        std::cerr << "Retry/check intervals too small. Use --open-retry-ms >=10, --configure-retry-ms >=10, --health-check-ms >=50." << std::endl;
        return false;
    }

    return true;
}

orcaSDK::OrcaError configure_motor(orcaSDK::Actuator& actuator, uint16_t damping)
{
    auto error = actuator.set_damper(damping);
    if (error) return add_config_step_context("set_damper", error);

    error = actuator.enable_haptic_effects(kDamperEffectBit);
    if (error) return add_config_step_context("enable_haptic_effects", error);

    actuator.update_haptic_stream_effects(kDamperEffectBit);

    error = actuator.set_mode(orcaSDK::HapticMode);
    if (error) return add_config_step_context("set_mode(HapticMode)", error);

    actuator.enable_stream();
    return {false, ""};
}

void run_motor_worker(
    MotorConfig motor_config,
    int baud_rate,
    int interframe_delay_us,
    uint16_t damping,
    int open_retry_ms,
    int configure_retry_ms,
    int health_check_ms,
    std::string serial_log_dir)
{
    const std::string label = motor_config.device + " (addr=" + std::to_string(motor_config.modbus_address) + ")";
    constexpr int kRetryLogIntervalMs = 2000;
    constexpr int kStatusLogIntervalMs = 3000;

    orcaSDK::Actuator actuator(label.c_str(), motor_config.modbus_address);

    const std::string serial_log_path = make_serial_log_path(serial_log_dir, motor_config);
    if (!serial_log_path.empty()) {
        const auto log_error = actuator.begin_serial_logging(serial_log_path);
        if (log_error) {
            log_line("[WARN] Raw serial logging disabled for " + label + ". Error: " + log_error.what());
        }
        else {
            log_line("[INFO] Raw serial log for " + label + " => " + serial_log_path);
        }
    }

    bool port_open = false;
    bool haptics_configured = false;
    bool waiting_for_port_logged = false;
    bool waiting_for_haptics_logged = false;
    MotorRunState state = MotorRunState::WaitingForSerial;
    auto next_open_attempt = std::chrono::steady_clock::now();
    auto next_config_attempt = std::chrono::steady_clock::now();
    auto next_health_check = std::chrono::steady_clock::now();
    auto next_open_retry_log = std::chrono::steady_clock::now();
    auto next_config_retry_log = std::chrono::steady_clock::now();
    auto next_status_log = std::chrono::steady_clock::now();

    log_line("[STATUS] " + label + " => " + motor_state_to_string(state));

    while (g_should_run.load()) {
        const auto now = std::chrono::steady_clock::now();

        if (!port_open && now >= next_open_attempt) {
            next_open_attempt = now + std::chrono::milliseconds(open_retry_ms);
            auto open_error = actuator.open_serial_port(motor_config.device, baud_rate, interframe_delay_us);
            if (open_error) {
                if (!waiting_for_port_logged || now >= next_open_retry_log) {
                    log_line("[WARN] " + label + " disconnected; retrying serial open every " + std::to_string(open_retry_ms) + "ms. Last error: " + open_error.what());
                    next_open_retry_log = now + std::chrono::milliseconds(kRetryLogIntervalMs);
                    waiting_for_port_logged = true;
                }
                continue;
            }

            port_open = true;
            haptics_configured = false;
            state = MotorRunState::WaitingForHaptics;
            next_config_attempt = now;
            waiting_for_haptics_logged = false;

            if (waiting_for_port_logged) {
                log_line("[INFO] Serial connection restored for " + label);
            }
            else {
                log_line("[INFO] Port opened for " + label);
            }
            waiting_for_port_logged = false;
            log_line("[STATUS] " + label + " => " + motor_state_to_string(state));
        }

        if (port_open && !haptics_configured && now >= next_config_attempt) {
            next_config_attempt = now + std::chrono::milliseconds(configure_retry_ms);
            auto config_error = configure_motor(actuator, damping);
            if (config_error) {
                if (!waiting_for_haptics_logged || now >= next_config_retry_log) {
                    log_line("[WARN] " + label + " connected but not configured; retrying haptics every " + std::to_string(configure_retry_ms) + "ms. Last error: " + config_error.what());
                    next_config_retry_log = now + std::chrono::milliseconds(kRetryLogIntervalMs);
                    waiting_for_haptics_logged = true;
                }

                if (should_reopen_port(config_error)) {
                    actuator.close_serial_port();
                    port_open = false;
                    haptics_configured = false;
                    state = MotorRunState::WaitingForSerial;
                    next_open_attempt = now + std::chrono::milliseconds(open_retry_ms);
                    waiting_for_port_logged = false;
                    log_line("[STATUS] " + label + " => " + motor_state_to_string(state));
                }
                continue;
            }

            haptics_configured = true;
            state = MotorRunState::HapticsActive;
            next_health_check = now + std::chrono::milliseconds(health_check_ms);
            waiting_for_haptics_logged = false;
            log_line("[INFO] Configured " + label + " in haptics mode with damping=" + std::to_string(damping));
            log_line("[STATUS] " + label + " => " + motor_state_to_string(state));
        }

        if (port_open) {
            actuator.run();
        }

        if (port_open && haptics_configured && now >= next_health_check) {
            next_health_check = now + std::chrono::milliseconds(health_check_ms);

            const auto mode = actuator.get_mode();
            if (mode.error) {
                log_line("[WARN] " + label + " lost communication while running; switching to retry mode. Error: " + mode.error.what());
                haptics_configured = false;
                state = MotorRunState::WaitingForHaptics;
                waiting_for_haptics_logged = false;
                next_config_attempt = now;

                if (should_reopen_port(mode.error)) {
                    actuator.close_serial_port();
                    port_open = false;
                    state = MotorRunState::WaitingForSerial;
                    next_open_attempt = now + std::chrono::milliseconds(open_retry_ms);
                    waiting_for_port_logged = false;
                }
                log_line("[STATUS] " + label + " => " + motor_state_to_string(state));
                continue;
            }

            if (mode.value != orcaSDK::HapticMode) {
                haptics_configured = false;
                state = MotorRunState::WaitingForHaptics;
                next_config_attempt = now;
                log_line("[STATUS] " + label + " => " + motor_state_to_string(state));
            }
        }

        if (now >= next_status_log) {
            next_status_log = now + std::chrono::milliseconds(kStatusLogIntervalMs);
            log_line("[STATUS] " + label + " => " + motor_state_to_string(state));
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    actuator.disable_stream();
    actuator.close_serial_port();
    log_line("[INFO] Stopped worker for " + label);
}

} // namespace

int main(int argc, char** argv)
{
    AppConfig config;
    if (!parse_args(argc, argv, config)) {
        print_usage(argv[0]);
        return 1;
    }

    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    log_line("[INFO] Starting ORCA haptics daemon for " + std::to_string(config.motors.size()) + " device(s)");
    log_line("[INFO] Options: baud=" + std::to_string(config.baud_rate)
        + " interframe-us=" + std::to_string(config.interframe_delay_us)
        + " damping=" + std::to_string(config.damping)
        + " open-retry-ms=" + std::to_string(config.open_retry_ms)
        + " configure-retry-ms=" + std::to_string(config.configure_retry_ms)
        + " health-check-ms=" + std::to_string(config.health_check_ms));
    if (!config.serial_log_dir.empty()) {
        log_line("[INFO] Raw serial logs directory: " + config.serial_log_dir);
    }

    std::vector<std::thread> workers;
    workers.reserve(config.motors.size());

    for (const auto& motor : config.motors) {
        workers.emplace_back(
            run_motor_worker,
            motor,
            config.baud_rate,
            config.interframe_delay_us,
            config.damping,
            config.open_retry_ms,
            config.configure_retry_ms,
            config.health_check_ms,
            config.serial_log_dir);
    }

    for (auto& worker : workers) {
        if (worker.joinable()) {
            worker.join();
        }
    }

    log_line("[INFO] ORCA haptics daemon exited cleanly");
    return 0;
}
