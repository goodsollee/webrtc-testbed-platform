#include "examples/peerconnection/client/sctp_traffic/file/file_receiver.h"

#include <algorithm>
#include <cctype>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <utility>

#include "absl/types/span.h"
#include "examples/peerconnection/client/conductor.h"
#include "rtc_base/logging.h"

namespace sctp::file {

Receiver::Receiver(int kind, std::string label, std::string output_dir)
    : kind_(kind),
      label_(std::move(label)),
      output_dir_(std::move(output_dir)) {}

Receiver::~Receiver() { Detach(); }

void Receiver::Attach(Conductor& c) {
  std::lock_guard<std::mutex> lock(state_mutex_);
  conductor_ = &c;

  const std::string path = MakeOutputPath();
  if (!path.empty()) {
    csv_file_.open(path, std::ios::out | std::ios::trunc);
    if (!csv_file_.is_open()) {
      RTC_LOG(LS_WARNING) << "[SCTP][FILE] Failed to open output file: "
                          << path;
      std::cout << "[SCTP][FILE][Receiver] Failed to open performance log file '"
                << path << "'" << std::endl;
    } else {
      csv_file_ << "timestamp_ms,chunk_bytes,total_bytes\n";
      csv_file_.flush();
      std::cout << "[SCTP][FILE][Receiver] Logging performance for '" << label_
                << "' to '" << path << "'" << std::endl;
    }
  }

  conductor_->RegisterPayloadHandler(
      static_cast<Conductor::TrafficKind>(kind_),
      [this](absl::Span<const uint8_t> bytes) { HandlePayload(bytes); });
}

void Receiver::Detach() {
  std::lock_guard<std::mutex> lock(state_mutex_);
  if (conductor_) {
    conductor_->RegisterPayloadHandler(static_cast<Conductor::TrafficKind>(kind_),
                                       Conductor::PayloadHandler());
  }
  conductor_ = nullptr;

  if (csv_file_.is_open()) {
    csv_file_.flush();
    csv_file_.close();
  }
  std::cout << "[SCTP][FILE][Receiver] Total bytes received for '" << label_
            << "': " << total_bytes_ << std::endl;
  total_bytes_ = 0;
  log_started_ = false;
}

void Receiver::HandlePayload(absl::Span<const uint8_t> bytes) {
  if (bytes.empty()) {
    return;
  }

  double timestamp_ms = 0.0;
  uint64_t total_bytes = 0;
  const size_t chunk_size = bytes.size();
  {
    std::lock_guard<std::mutex> lock(state_mutex_);
    const auto now = std::chrono::steady_clock::now();
    if (!log_started_) {
      start_time_ = now;
      log_started_ = true;
    }
    timestamp_ms =
        std::chrono::duration<double, std::milli>(now - start_time_).count();
    total_bytes_ += chunk_size;
    total_bytes = total_bytes_;
    if (csv_file_.is_open()) {
      csv_file_ << std::fixed << std::setprecision(3) << timestamp_ms << ","
                << chunk_size << "," << total_bytes << "\n";
      csv_file_.flush();
    }
  }

  std::ostringstream oss;
  oss << "[SCTP][FILE][Receiver] label='" << label_ << "' time_ms="
      << std::fixed << std::setprecision(3) << timestamp_ms
      << " chunk_bytes=" << chunk_size << " total_bytes=" << total_bytes;
  std::cout << oss.str() << std::endl;
}

std::string Receiver::MakeOutputPath() const {
  std::string base = label_;
  if (base.empty()) {
    base = "sctp_flow";
  }
  std::replace(base.begin(), base.end(), '/', '_');
  std::replace(base.begin(), base.end(), '\\', '_');
  for (char& ch : base) {
    if (!std::isalnum(static_cast<unsigned char>(ch)) && ch != '_' &&
        ch != '-') {
      ch = '_';
    }
  }

  if (output_dir_.empty()) {
    return base + "_rx.csv";
  }
  std::string path = output_dir_;
  if (!path.empty() && path.back() != '/' && path.back() != '\\') {
    path.push_back('/');
  }
  path += base + "_rx.csv";
  return path;
}

}  // namespace sctp::file

