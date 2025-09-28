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
#include "examples/peerconnection/client/conductor.h"  // Need full declaration for TrafficKind

class Conductor;  // Forward declaration still needed

namespace sctp::file {

// Sends dummy file data over SCTP according to a traffic profile.
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

  // Periodic pattern constructor.
  Sender(int kind, int file_size, int periodicity_ms);
  // Custom trace pattern constructor (ABSOLUTE time).
  // Trace CSV rows: time_ms,size_bytes
  Sender(int kind, const std::string& trace_path);
  ~Sender();

  void Start(Conductor& c);
  void Stop();

 private:
  enum class Mode { kPeriodic, kCustom };
  void RunPeriodic();
  void RunCustom();                 // absolute-time scheduler
  void LoadTrace(const std::string& path);  // parses time_ms,size
  void WaitForBufferSpace(Conductor::TrafficKind kind);  // Added: flow control method
  
  struct LogEntry {
    double timestamp_ms = 0.0;
    uint64_t total_data_bytes = 0;
  };

  LogEntry LogSendEvent(const PayloadMetadata& metadata);
  void SendFile(size_t file_bytes);
  std::vector<uint8_t> BuildChunkPayload(const PayloadMetadata& metadata);
  std::string MakeLogPath() const;

  Conductor* conductor_ = nullptr;
  int kind_;
  int file_size_ = 0;
  int periodicity_ms_ = 0;

  // Absolute schedule: (time_ms_since_start, size_bytes)
  std::vector<std::pair<int, int>> custom_events_;

  std::thread worker_;
  std::atomic<bool> running_{false};
  Mode mode_;

  std::mutex log_mutex_;
  std::ofstream csv_log_;
  bool log_started_ = false;
  std::chrono::steady_clock::time_point log_start_time_;
  std::string log_path_;
  uint64_t total_data_bytes_sent_ = 0;
  uint64_t next_sequence_ = 0;
  std::chrono::steady_clock::time_point flow_start_time_;
  bool flow_start_time_initialized_ = false;
};

}  // namespace sctp::file