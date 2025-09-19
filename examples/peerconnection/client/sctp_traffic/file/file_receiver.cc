#include "examples/peerconnection/client/sctp_traffic/file/file_receiver.h"

#include <algorithm>
#include <cctype>
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
    output_file_.open(path, std::ios::binary | std::ios::out | std::ios::trunc);
    if (!output_file_.is_open()) {
      RTC_LOG(LS_WARNING) << "[SCTP][FILE] Failed to open output file: "
                          << path;
    } else {
      RTC_LOG(LS_INFO) << "[SCTP][FILE] Receiving '" << label_
                       << "' into " << path;
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

  if (output_file_.is_open()) {
    RTC_LOG(LS_INFO) << "[SCTP][FILE] Total bytes received for '" << label_
                     << "': " << total_bytes_;
    output_file_.close();
  }
  total_bytes_ = 0;
  next_log_threshold_ = 1 << 20;
}

void Receiver::HandlePayload(absl::Span<const uint8_t> bytes) {
  if (bytes.empty()) {
    return;
  }

  std::lock_guard<std::mutex> lock(state_mutex_);
  if (!output_file_.is_open()) {
    total_bytes_ += bytes.size();
    return;
  }

  output_file_.write(reinterpret_cast<const char*>(bytes.data()),
                     bytes.size());

  total_bytes_ += bytes.size();
  if (total_bytes_ >= next_log_threshold_) {
    RTC_LOG(LS_INFO) << "[SCTP][FILE] Received " << total_bytes_
                     << " bytes for '" << label_ << "'";
    output_file_.flush();
    next_log_threshold_ += (1 << 20);
  }
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
    return base + "_rx.bin";
  }
  std::string path = output_dir_;
  if (!path.empty() && path.back() != '/' && path.back() != '\\') {
    path.push_back('/');
  }
  path += base + "_rx.bin";
  return path;
}

}  // namespace sctp::file

