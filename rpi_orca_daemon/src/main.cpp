#include "actuator.h"

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <iostream>
#include <sstream>
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
};

constexpr uint16_t kDamperEffectBit = (1u << 4);

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
        << "  --device <path>                 Linux device node (e.g., /dev/orca_front)\n\n"
        << "Options:\n"
        << "  --baud <int>                    Default: 19200\n"
        << "  --interframe-us <int>           Default: 2000\n"
        << "  --damping <int>                 Default: 600\n"
        << "  --address <int>                 Apply one Modbus address to all devices (default: 1)\n"
        << "  --device-address <path:addr>    Per-device Modbus address override\n"
        << std::endl;
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

    return true;
}

orcaSDK::OrcaError configure_motor(orcaSDK::Actuator& actuator, uint16_t damping)
{
    auto error = actuator.set_damper(damping);
    if (error) return error;

    error = actuator.enable_haptic_effects(kDamperEffectBit);
    if (error) return error;

    actuator.update_haptic_stream_effects(kDamperEffectBit);

    error = actuator.set_mode(orcaSDK::HapticMode);
    if (error) return error;

    actuator.enable_stream();
    return {false, ""};
}

void run_motor_worker(MotorConfig motor_config, int baud_rate, int interframe_delay_us, uint16_t damping)
{
    const std::string label = motor_config.device + " (addr=" + std::to_string(motor_config.modbus_address) + ")";
    orcaSDK::Actuator actuator(label.c_str(), motor_config.modbus_address);

    bool connected = false;
    auto next_health_check = std::chrono::steady_clock::now();

    while (g_should_run.load()) {
        if (!connected) {
            auto open_error = actuator.open_serial_port(motor_config.device, baud_rate, interframe_delay_us);
            if (open_error) {
                log_line("[WARN] " + label + " open failed: " + open_error.what() + " ; retrying in 2s");
                std::this_thread::sleep_for(std::chrono::seconds(2));
                continue;
            }

            auto config_error = configure_motor(actuator, damping);
            if (config_error) {
                log_line("[WARN] " + label + " configure failed: " + config_error.what() + " ; retrying in 2s");
                actuator.close_serial_port();
                std::this_thread::sleep_for(std::chrono::seconds(2));
                continue;
            }

            connected = true;
            log_line("[INFO] Connected and configured " + label + " in haptics mode with damping=" + std::to_string(damping));
            next_health_check = std::chrono::steady_clock::now() + std::chrono::seconds(2);
        }

        actuator.run();

        const auto now = std::chrono::steady_clock::now();
        if (now >= next_health_check) {
            next_health_check = now + std::chrono::seconds(2);

            const auto mode = actuator.get_mode();
            if (mode.error) {
                log_line("[WARN] " + label + " communication health-check failed: " + mode.error.what() + " ; reconnecting");
                actuator.close_serial_port();
                connected = false;
                std::this_thread::sleep_for(std::chrono::seconds(1));
                continue;
            }

            if (mode.value != orcaSDK::HapticMode) {
                log_line("[WARN] " + label + " not in HapticMode anymore; reconfiguring");
                auto config_error = configure_motor(actuator, damping);
                if (config_error) {
                    log_line("[WARN] " + label + " reconfigure failed: " + config_error.what() + " ; reconnecting");
                    actuator.close_serial_port();
                    connected = false;
                    std::this_thread::sleep_for(std::chrono::seconds(1));
                    continue;
                }
            }
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

    std::vector<std::thread> workers;
    workers.reserve(config.motors.size());

    for (const auto& motor : config.motors) {
        workers.emplace_back(run_motor_worker, motor, config.baud_rate, config.interframe_delay_us, config.damping);
    }

    for (auto& worker : workers) {
        if (worker.joinable()) {
            worker.join();
        }
    }

    log_line("[INFO] ORCA haptics daemon exited cleanly");
    return 0;
}
