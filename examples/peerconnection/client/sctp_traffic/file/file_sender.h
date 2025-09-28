#pragma once

#include <atomic>
#include <chrono>
#include <fstream>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

class Conductor;  // Forward declaration.

namespace sctp::file {

// Sends dummy file data over SCTP according to a traffic profile.
class Sender {
 public:
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
  void LogSendEvent(size_t bytes);
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
  uint64_t total_bytes_sent_ = 0;
};

}  // namespace sctp::file
