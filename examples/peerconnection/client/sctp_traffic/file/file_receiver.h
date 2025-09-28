#pragma once

#include <cstdint>
#include <fstream>
#include <mutex>
#include <string>

#include "examples/peerconnection/client/sctp_traffic/traffic.h"

class Conductor;  // Forward declaration.

namespace sctp::file {

// Receives SCTP "file" traffic and optionally persists it to disk.
class Receiver final : public sctp::Receiver {
 public:
  Receiver(int kind, std::string label, std::string output_dir, int slo_ms);
  ~Receiver() override;

  void Attach(Conductor& c) override;
  void Detach() override;

 private:
  void HandlePayload(absl::Span<const uint8_t> bytes);
  std::string MakeOutputPath() const;

  Conductor* conductor_ = nullptr;
  int kind_;
  std::string label_;
  std::string output_dir_;
  int slo_ms_;

  std::mutex state_mutex_;
  std::ofstream csv_file_;
  uint64_t total_data_bytes_ = 0;
  uint64_t total_files_received_ = 0;
  uint64_t slo_met_count_ = 0;
  bool log_started_ = false;
  bool sender_time_initialized_ = false;
  uint64_t first_sender_timestamp_ms_ = 0;
  bool sender_start_time_initialized_ = false;
  int64_t sender_start_time_ms_ = 0;
};

}  // namespace sctp::file

