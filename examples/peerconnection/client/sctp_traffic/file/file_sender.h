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

  // Enhanced backpressure configuration
  struct BackpressureConfig {
    uint64_t buffer_threshold = 16 * 1024 * 1024;   // 8MB - more aggressive than 16MB
    uint64_t buffer_target = 8 * 1024 * 1024;      // 3MB - target level after draining
    int base_check_interval_ms = 1;                 // Base polling interval
    int max_check_interval_ms = 8;                  // Max adaptive interval
    size_t batch_size = 16;                          // Send multiple chunks per batch
    bool adaptive_batching = true;                  // Enable adaptive batch sizing
    bool use_exponential_backoff = true;            // Use exponential backoff when blocked
  };

  void RunPeriodic();
  void RunCustom();                 // absolute-time scheduler
  void LoadTrace(const std::string& path);  // parses time_ms,size

  // Enhanced backpressure methods
  bool CheckBufferSpaceNonBlocking();
  bool WaitForBufferSpaceAdaptive();
  void AdaptBackpressureParameters(uint64_t current_buffered);

  // Token bucket rate limiting helpers
  double ComputeTargetBitrateBps(size_t file_bytes) const;
  void ResetRateLimiter(size_t max_chunk_payload, size_t file_bytes);
  void UpdateRateBudget(std::chrono::steady_clock::time_point now);
  bool ConsumeRateBudget(size_t chunk_bytes);

  struct LogEntry {
    double timestamp_ms = 0.0;
    uint64_t total_data_bytes = 0;
  };

  LogEntry LogSendEvent(const PayloadMetadata& metadata);
  void SendFile(size_t file_bytes);
  void SendFileBatched(size_t file_bytes);
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

  // Enhanced backpressure state
  BackpressureConfig backpressure_config_;
  size_t current_batch_size_ = 1;
  int current_check_interval_ms_ = 1;
  int consecutive_blocks_ = 0;  // For exponential backoff

  struct TokenBucketConfig {
    int burst_window_ms = 200;
    size_t min_bucket_bytes = 64 * 1024;
    size_t max_bucket_bytes = 32 * 1024 * 1024;
    size_t min_burst_chunks = 4;
  };
  TokenBucketConfig token_bucket_config_;
  bool rate_limiter_enabled_ = false;
  double current_target_bitrate_bps_ = 0.0;
  double rate_bucket_bytes_ = 0.0;
  size_t rate_bucket_capacity_bytes_ = 0;
  double bucket_fill_rate_bytes_per_second_ = 0.0;
  std::chrono::steady_clock::time_point last_rate_update_time_;
  bool rate_bucket_initialized_ = false;
  double last_logged_target_bps_ = 0.0;

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