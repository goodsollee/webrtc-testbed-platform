#pragma once

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "examples/peerconnection/client/sctp_traffic/file/file_message.h"

class Conductor;  // Forward declaration for pointer member

namespace sctp::file {

// High-throughput SCTP file sender optimized for maximum bandwidth utilization
class Sender {
 public:
  struct PayloadMetadata {
    uint64_t sequence = 0;
    uint64_t send_time_ms = 0;
    uint64_t file_bytes = 0;
    uint64_t chunk_bytes = 0;
    uint32_t chunk_index = 0;
    uint32_t chunk_count = 0;
  };

  // Periodic pattern constructor
  Sender(int kind, int file_size, int periodicity_ms);
  // Custom trace pattern constructor (ABSOLUTE time)
  // Trace CSV rows: time_ms,size_bytes
  Sender(int kind, const std::string& trace_path);
  ~Sender();

  void Start(Conductor& c);
  void Stop();
  void EnableHighSpeedMode(bool enable = true);

 private:
  enum class Mode { kPeriodic, kCustom };

  // Optimized configuration for maximum throughput
  struct Config {
    static constexpr uint64_t MAX_BUFFER_THRESHOLD = 16 * 1024 * 1024;   // 16MB
    static constexpr uint64_t BUFFER_CHECK_THRESHOLD = 8 * 1024 * 1024;  // 8MB
    static constexpr size_t MAX_BATCH_SIZE = 512;    // Limit bursts for fairness
    static constexpr size_t MIN_BATCH_SIZE = 32;     // Minimum batch size
    static constexpr int BUFFER_CHECK_INTERVAL = 16; // Check regularly
    static constexpr size_t LOG_INTERVAL = 1000;   // Log every N chunks
  };

  void RunPeriodic();
  void RunCustom();
  void LoadTrace(const std::string& path);
  
  // Core transmission methods
  void SendFileBatched(size_t file_bytes);
  bool WaitForBufferSpace();
  bool IsFlowReady();
  
  // Utility methods
  std::vector<uint8_t> BuildChunkPayload(const PayloadMetadata& metadata);
  void LogSendEvent(const PayloadMetadata& metadata);
  std::string MakeLogPath() const;

  // Core state
  Conductor* conductor_ = nullptr;
  int kind_;
  int file_size_ = 0;
  int periodicity_ms_ = 0;
  std::vector<std::pair<int, int>> custom_events_;
  std::thread worker_;
  std::atomic<bool> running_{false};
  Mode mode_;
  bool high_speed_mode_ = true;

  // Performance tracking
  uint64_t total_data_bytes_sent_ = 0;
  uint64_t next_sequence_ = 0;
  std::chrono::steady_clock::time_point flow_start_time_;
  bool flow_start_time_initialized_ = false;

  // Minimal logging for performance
  std::mutex log_mutex_;
  std::ofstream csv_log_;
  std::string log_path_;
  bool log_started_ = false;
  std::chrono::steady_clock::time_point log_start_time_;
};

}  // namespace sctp::file