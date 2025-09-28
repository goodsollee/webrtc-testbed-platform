#pragma once

#include <chrono>
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
  Receiver(int kind, std::string label, std::string output_dir);
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

  std::mutex state_mutex_;
  std::ofstream csv_file_;
  uint64_t total_bytes_ = 0;
  bool log_started_ = false;
  std::chrono::steady_clock::time_point start_time_;
};

}  // namespace sctp::file

