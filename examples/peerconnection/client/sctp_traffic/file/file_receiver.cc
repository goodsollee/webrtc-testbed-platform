#include "examples/peerconnection/client/sctp_traffic/file/file_receiver.h"

#include <algorithm>
#include <cctype>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <utility>

#include "absl/types/span.h"
#include "examples/peerconnection/client/conductor.h"
#include "modules/rtp_rtcp/source/byte_io.h"
#include "rtc_base/byte_order.h"
#include "rtc_base/logging.h"
#include "rtc_base/time_utils.h"

namespace sctp::file {

namespace {
constexpr size_t kHeaderSize = sizeof(uint64_t) * 2;  // sequence + send_time
}

Receiver::Receiver(int kind,
                   std::string label,
                   std::string output_dir,
                   int slo_ms)
    : kind_(kind),
      label_(std::move(label)),
      output_dir_(std::move(output_dir)),
      slo_ms_(slo_ms) {}

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
      csv_file_ << "timestamp_ms,data_bytes,total_data_bytes,sequence,send_time_ms,";
      csv_file_ << "transmit_start_time_ms,transmit_end_time_ms,delivery_delay_ms,";
      csv_file_ << "slo_ms,slo_satisfied,slo_satisfaction_ratio\n";
      csv_file_.flush();
      std::cout << "[SCTP][FILE][Receiver] Logging performance for '" << label_
                << "' to '" << path << "'";
      if (slo_ms_ > 0) {
        std::cout << " (SLO=" << slo_ms_ << " ms)";
      }
      std::cout << std::endl;
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
  double ratio = 0.0;
  if (total_files_received_ > 0) {
    ratio = static_cast<double>(slo_met_count_) /
            static_cast<double>(total_files_received_);
  }
  std::ostringstream summary;
  summary << "[SCTP][FILE][Receiver] Total data bytes received for '" << label_
          << "': " << total_data_bytes_ << ". Files=" << total_files_received_
          << " slo_met=" << slo_met_count_;
  if (total_files_received_ > 0) {
    summary << " ratio=" << std::fixed << std::setprecision(3) << ratio;
  }
  std::cout << summary.str() << std::endl;
  total_data_bytes_ = 0;
  total_files_received_ = 0;
  slo_met_count_ = 0;
  log_started_ = false;
  sender_time_initialized_ = false;
  sender_start_time_initialized_ = false;
  first_sender_timestamp_ms_ = 0;
  sender_start_time_ms_ = 0;
}

void Receiver::HandlePayload(absl::Span<const uint8_t> bytes) {
  if (bytes.empty()) {
    return;
  }

  if (bytes.size() < kHeaderSize) {
    RTC_LOG(LS_WARNING) << "[SCTP][FILE] Payload too small to contain header.";
    return;
  }

  const uint64_t sequence =
      webrtc::ByteReader<uint64_t>::ReadLittleEndian(bytes.data());
  const uint64_t send_time_ms =
      webrtc::ByteReader<uint64_t>::ReadLittleEndian(bytes.data() + sizeof(uint64_t));
  const size_t data_bytes = bytes.size() - kHeaderSize;
  double delivery_delay_ms = 0.0;
  int64_t arrival_time_ms = rtc::TimeMillis();
  int64_t transmit_start_time_ms = 0;
  int64_t transmit_end_time_ms = 0;
  uint64_t total_data_bytes = 0;
  uint64_t files_received = 0;
  uint64_t slo_met = 0;
  bool slo_satisfied = true;
  double slo_ratio = 0.0;
  {
    std::lock_guard<std::mutex> lock(state_mutex_);
    if (!log_started_) {
      log_started_ = true;
    }

    if (!sender_time_initialized_) {
      sender_time_initialized_ = true;
      first_sender_timestamp_ms_ = send_time_ms;
    }
    int64_t sender_relative_ms = 0;
    if (send_time_ms >= first_sender_timestamp_ms_) {
      sender_relative_ms =
          static_cast<int64_t>(send_time_ms - first_sender_timestamp_ms_);
    }
    if (!sender_start_time_initialized_) {
      sender_start_time_initialized_ = true;
      sender_start_time_ms_ = arrival_time_ms - sender_relative_ms;
    }
    transmit_start_time_ms = sender_start_time_ms_;
    transmit_end_time_ms = sender_start_time_ms_ + sender_relative_ms;
    delivery_delay_ms =
        static_cast<double>(std::max<int64_t>(0, arrival_time_ms - transmit_end_time_ms));

    ++total_files_received_;
    if (slo_ms_ > 0) {
      if (delivery_delay_ms <= static_cast<double>(slo_ms_)) {
        ++slo_met_count_;
        slo_satisfied = true;
      } else {
        slo_satisfied = false;
      }
    } else {
      ++slo_met_count_;
      slo_satisfied = true;
    }

    total_data_bytes_ += data_bytes;
    total_data_bytes = total_data_bytes_;
    files_received = total_files_received_;
    slo_met = slo_met_count_;
    if (files_received > 0) {
      slo_ratio = static_cast<double>(slo_met) /
                  static_cast<double>(files_received);
    }
    if (csv_file_.is_open()) {
      csv_file_ << arrival_time_ms << "," << data_bytes << ","
                << total_data_bytes << ","
                << sequence << "," << send_time_ms << ","
                << transmit_start_time_ms << ","
                << transmit_end_time_ms << ","
                << std::fixed << std::setprecision(3) << delivery_delay_ms
                << "," << slo_ms_ << ","
                << (slo_satisfied ? "true" : "false") << ","
                << std::fixed << std::setprecision(6) << slo_ratio << "\n";
      csv_file_.flush();
    }
  }

  std::ostringstream oss;
  oss << "[SCTP][FILE][Receiver] label='" << label_ << "' time_ms="
      << arrival_time_ms
      << " data_bytes=" << data_bytes << " total_data_bytes="
      << total_data_bytes << " seq=" << sequence
      << " send_time_ms=" << send_time_ms
      << " delivery_delay_ms=" << std::fixed << std::setprecision(3)
      << delivery_delay_ms;
  oss << " slo_ms=" << slo_ms_ << " satisfied="
      << (slo_satisfied ? "true" : "false") << " ratio="
      << std::fixed << std::setprecision(3) << slo_ratio;
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

