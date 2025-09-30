#include "network_emulator.h"
#include <iostream>
#include <memory>
#include <csignal>
#include <algorithm>
#include <cctype>
#include <unistd.h>
#include <sys/select.h>

#include "absl/flags/flag.h"
#include "absl/flags/parse.h"

// Define Abseil Flags
ABSL_FLAG(std::string, profile_path, "", "Path to the network profile CSV file (optional)");
ABSL_FLAG(std::string, interface_name, "", "Network interface name to be emulated (mandatory)");

// Global emulator instance
std::unique_ptr<NetworkEmulator> g_emulator;

// Signal handler for graceful shutdown
void SignalHandler(int signum) {
    if (g_emulator) {
        LOG_INFO("main", "Received signal ", signum, ", cleaning up...");
        g_emulator->Stop();
        g_emulator.reset();
    }
    LOG_INFO("main", "Exiting with signal ", signum);
    exit(signum);
}

std::string GetDefaultInterface() {
    // Get default route interface
    FILE* pipe = popen("ip route | grep default | awk '{print $5}'", "r");
    if (!pipe) return "";
    
    char buffer[128];
    std::string result = "";
    if (fgets(buffer, sizeof(buffer), pipe) != NULL) {
        result = buffer;
        // Remove newline
        result = result.substr(0, result.find('\n'));
    }
    pclose(pipe);
    return result;
}


int main(int argc, char* argv[]) {
    // Parse command-line arguments
    absl::ParseCommandLine(argc, argv);

    // Retrieve flag values
    std::string profile_path = absl::GetFlag(FLAGS_profile_path);
    std::string interface_name = absl::GetFlag(FLAGS_interface_name);

    // Auto-detect if not specified
    if (interface_name.empty()) {
        interface_name = GetDefaultInterface();
        if (interface_name.empty()) {
            std::cerr << "Error: Could not detect default interface and none specified\n";
            return 1;
        }
        LOG_INFO("main", "Using detected interface: ", interface_name);
    }

    LOG_INFO("main", "Interface: ", interface_name);

    if (!profile_path.empty()) {
        LOG_INFO("main", "Using profile path: ", profile_path);
    } else {
        LOG_INFO("main", "No profile path provided. Running without a network profile.");
    }

    // Register signal handlers
    signal(SIGINT, SignalHandler);
    signal(SIGTERM, SignalHandler);

    try {
        // Initialize logging
        Logger::getInstance().setLogFile("network_emulator.log");
        LOG_INFO("main", "Starting Network Emulator");

        // Create and initialize the emulator
        g_emulator = std::make_unique<NetworkEmulator>();
        // Generate a unique name for the peer interface
        std::string peer_name = interface_name + "_peer";
        
        if (!g_emulator->Initialize(profile_path, interface_name, peer_name)) {
            LOG_ERROR("main", "Failed to initialize network emulator");
            return 1;
        }

        // Wait for external trigger to start
        LOG_INFO("main", "Waiting for 'start' command...");

        auto normalize_input = [](std::string input) {
            auto trim = [](const std::string& value) {
                const auto first = value.find_first_not_of(" \t\r\n");
                if (first == std::string::npos) {
                    return std::string();
                }
                const auto last = value.find_last_not_of(" \t\r\n");
                return value.substr(first, last - first + 1);
            };

            std::string trimmed = trim(input);
            std::transform(trimmed.begin(), trimmed.end(), trimmed.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            return trimmed;
        };

        auto wait_for_start = [&](bool interactive) {
        while (true) {
            std::string input;
            if (interactive) {
                std::cout << "Type 'start' to begin traffic shaping: " << std::flush;
                if (!std::getline(std::cin, input)) {
                    return false; // stdin closed
                }
            } else {
                fd_set readfds;
                FD_ZERO(&readfds);
                FD_SET(STDIN_FILENO, &readfds);

                struct timeval timeout;
                timeout.tv_sec = 0;
                timeout.tv_usec = 100000; // 100ms

                int ret = select(STDIN_FILENO + 1, &readfds, NULL, NULL, &timeout);
                if (ret <= 0 || !FD_ISSET(STDIN_FILENO, &readfds)) {
                    continue; // nothing to read
                }

                if (!std::getline(std::cin, input)) {
                    return false; // stdin closed
                }
            }

            // Normalize and check
            if (normalize_input(input) == "start") {
                return true;
            }

            // Ignore everything else
            }
        };


        // Check if stdin is a terminal (interactive) or redirected
        bool stdin_available = isatty(fileno(stdin));

        if (!wait_for_start(stdin_available)) {
            LOG_ERROR("main", "Failed to receive 'start' command from stdin");
            return 1;
        }

        // Start the emulator
        g_emulator->Start();
        LOG_INFO("main", "Network emulator running. Press Ctrl+C to stop...");

        // Keep the main thread alive
        while (g_emulator && g_emulator->IsRunning()) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }

    } catch (const std::exception& e) {
        LOG_ERROR("main", "Error: ", e.what());
        return 1;
    }

    LOG_INFO("main", "Exiting program");
    return 0;
}