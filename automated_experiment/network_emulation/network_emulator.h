#ifndef NETWORK_EMULATOR_H_
#define NETWORK_EMULATOR_H_

#include <string>
#include <thread>
#include <atomic>
#include <vector>
#include <memory>
#include <algorithm> // For sorting
#include <chrono>
#include "../../logger/Logger.h"

class NetworkEmulator {
public:
    struct NetworkProfile {
        int64_t timestamp_ms;
        double bandwidth_kbps;
        double latency_ms;
    };

    NetworkEmulator();
    ~NetworkEmulator();

    // Initialize emulator with network profile and interface names
    bool Initialize(const std::string& profile_path, 
                   const std::string& interface_name,
                   const std::string& peer_interface_name);

    // Interface management
    bool CreateVirtualInterface();
    void DeleteVirtualInterface();

    // Emulation control
    void Start();
    void Stop();

    // Getters for interface info
    std::string GetInterfaceName() const { return interface_name_; }
    std::string GetPeerInterfaceName() const { return peer_interface_name_; }
    bool IsRunning() const { return is_running_; }

private:
    bool ParseProfileFile();
    void EmulationLoop();
    void ApplyNetworkConditions(double bandwidth_kbps, double latency_ms);

    std::string profile_path_;
    std::string interface_name_;
    std::string peer_interface_name_;
    std::vector<NetworkProfile> network_profiles_;
    std::thread emulation_thread_;
    std::atomic<bool> is_running_;
    size_t current_profile_index_;
    bool qdisc_installed_;
    double last_bandwidth_kbps_;
    double last_latency_ms_;
    std::chrono::steady_clock::time_point last_update_time_;
};

#endif // NETWORK_EMULATOR_H_
