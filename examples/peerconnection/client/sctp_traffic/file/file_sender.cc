#include "examples/peerconnection/client/sctp_traffic/file/file_sender.h"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <thread>

#include "absl/types/span.h"
#include "examples/peerconnection/client/conductor.h"
#include "modules/rtp_rtcp/source/byte_io.h"
#include "rtc_base/byte_order.h"
#include "rtc_base/logging.h"

static bool ParseIntStrict(const std::string& s, int& out) {
  // Reject empty
  if (s.empty()) return false;

  // Trim is already done before calling this, but be safe:
  size_t i = 0, j = s.size();
  while (i < j && std::isspace(static_cast<unsigned char>(s[i]))) ++i;
  while (j > i && std::isspace(static_cast<unsigned char>(s[j-1]))) --j;
  if (i >= j) return false;

  errno = 0;
  char* end = nullptr;
  long v = std::strtol(s.c_str() + i, &end, 10);

  // Must consume all chars (no trailing junk)
  if (errno != 0) return false;
  if (end == (s.c_str() + i)) return false;
  // Allow trailing spaces only
  while (*end && std::isspace(static_cast<unsigned char>(*end))) ++end;
  if (*end != '\0') return false;

  if (v < INT_MIN || v > INT_MAX) return false;
  out = static_cast<int>(v);
  return true;
}

namespace sctp::file {

// Buffer threshold - when buffer exceeds this, wait for it to drain
// This is conservative to avoid overwhelming the channel
static constexpr uint64_t kMaxBufferedBytes = 16 * 1024 * 1024;  // 16MB buffer limit
static constexpr int kBufferCheckIntervalMs = 1;  // Check buffer every 1ms when waiting

Sender::Sender(int kind, int file_size, int periodicity_ms)
    : kind_(kind), file_size_(file_size), periodicity_ms_(periodicity_ms),
      mode_(Mode::kPeriodic) {}

Sender::Sender(int kind, const std::string& trace_path)
    : kind_(kind), mode_(Mode::kCustom) {
  LoadTrace(trace_path);  // now parses absolute times: time_ms,size
}

Sender::~Sender() { Stop(); }

void Sender::Start(Conductor& c) {
  conductor_ = &c;
  running_.store(true);
  std::string log_message;
  {
    std::lock_guard<std::mutex> lock(log_mutex_);
    total_data_bytes_sent_ = 0;
    log_started_ = false;
    log_path_ = MakeLogPath();
    csv_log_.open(log_path_, std::ios::out | std::ios::trunc);
    if (csv_log_.is_open()) {
      csv_log_ << "timestamp_ms,chunk_bytes,total_data_bytes,file_bytes,sequence,chunk_index,chunk_count,send_time_ms\n";
      csv_log_.flush();
      log_message = "[SCTP][FILE][Sender] Logging performance data to '" +
                    log_path_ + "'";
    } else {
      log_message =
          "[SCTP][FILE][Sender] Failed to open performance log file '" +
          log_path_ + "'";
    }
  }
  std::cout << log_message << std::endl;
  if (mode_ == Mode::kPeriodic) {
    worker_ = std::thread([this]() { RunPeriodic(); });
  } else {
    worker_ = std::thread([this]() { RunCustom(); });
  }
}

void Sender::Stop() {
  running_.store(false);
  if (worker_.joinable())
    worker_.join();
  uint64_t total_bytes = 0;
  std::string path;
  {
    std::lock_guard<std::mutex> lock(log_mutex_);
    if (csv_log_.is_open()) {
      csv_log_.flush();
      csv_log_.close();
    }
    total_bytes = total_data_bytes_sent_;
    path = log_path_;
  }
  std::ostringstream oss;
  oss << "[SCTP][FILE][Sender] kind=" << kind_
      << " stopped after sending " << total_bytes << " data bytes";
  if (!path.empty()) {
    oss << ". Log file: '" << path << "'";
  }
  std::cout << oss.str() << std::endl;
}

void Sender::RunPeriodic() {
  while (running_.load()) {
    if (!conductor_->IsFlowOpen(static_cast<Conductor::TrafficKind>(kind_))) {
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
      continue;
    }
    SendFile(file_size_);
    std::this_thread::sleep_for(std::chrono::milliseconds(periodicity_ms_));
  }
}

// Parse absolute schedule: "time_ms,size"
void Sender::LoadTrace(const std::string& path) {
  std::ifstream file(path);
  if (!file.is_open())
    return;

  auto trim = [](std::string& s) {
    auto issp = [](unsigned char c){ return std::isspace(c); };
    s.erase(s.begin(), std::find_if(s.begin(), s.end(), [&](unsigned char c){ return !issp(c); }));
    s.erase(std::find_if(s.rbegin(), s.rend(), [&](unsigned char c){ return !issp(c); }).base(), s.end());
  };

  std::string line;
  bool first = true;
  while (std::getline(file, line)) {
    if (line.empty())
      continue;

    // Skip a header line that contains alphabetic chars
    if (first) {
      first = false;
      if (std::any_of(line.begin(), line.end(),
                      [](unsigned char c){ return std::isalpha(c); })) {
        continue;
      }
    }

    std::istringstream ss(line);
    std::string t_s, size_s;
    if (!std::getline(ss, t_s, ',')) continue;
    if (!std::getline(ss, size_s, ',')) continue;
    trim(t_s); trim(size_s);

    int t = 0, sz = 0;
    if (!ParseIntStrict(t_s, t)) continue;
    if (!ParseIntStrict(size_s, sz)) continue;
    if (t < 0 || sz < 0) continue;

    // Absolute schedule: (time_ms_since_start, size_bytes)
    custom_events_.emplace_back(t, sz);
  }

  std::sort(custom_events_.begin(), custom_events_.end(),
            [](const auto& a, const auto& b){ return a.first < b.first; });
}

// Absolute-time scheduler.
// For each (t_ms, size): wait until t0 + t_ms, then send size bytes.
// t0 is defined as the moment the data channel becomes open.
void Sender::RunCustom() {
  using clock = std::chrono::steady_clock;

  // Wait for channel to open; define t0 once we can send.
  while (running_.load() &&
         !conductor_->IsFlowOpen(static_cast<Conductor::TrafficKind>(kind_))) {
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  if (!running_.load()) return;

  const auto t0 = clock::now();

  for (const auto& ev : custom_events_) {
    if (!running_.load()) break;

    const auto due = t0 + std::chrono::milliseconds(ev.first);

    // Sleep until due time (cooperative sleep to respond to Stop()).
    for (;;) {
      if (!running_.load()) break;

      // If channel closed mid-run, just keep waiting for time; you could add
      // logic to pause/shift/drop if you want different behavior.
      const auto now = clock::now();
      if (now >= due) break;

      auto rem = std::chrono::duration_cast<std::chrono::milliseconds>(due - now);
      std::this_thread::sleep_for(rem > std::chrono::milliseconds(5)
                                      ? std::chrono::milliseconds(5)
                                      : rem);
    }
    if (!running_.load()) break;

    // Send at/after the due time (best-effort even if late).
    if (conductor_->IsFlowOpen(static_cast<Conductor::TrafficKind>(kind_))) {
      SendFile(ev.second);
    } else {
      // Channel closed right at the send time; skip or busy-wait here if preferred.
      // (Current policy: skip the send if the flow isn't open at due time.)
    }
  }
}

void Sender::WaitForBufferSpace() {
  if (!conductor_) {
    return;
  }

  const auto kind = static_cast<Conductor::TrafficKind>(kind_);

  // Wait until the buffer has drained sufficiently
  while (running_.load()) {
    if (!conductor_->IsFlowOpen(kind)) {
      // Flow closed temporarily; avoid tight loop and allow re-check later.
      std::this_thread::sleep_for(std::chrono::milliseconds(kBufferCheckIntervalMs));
      continue;
    }

    uint64_t buffered = conductor_->BufferedAmount(kind);
    if (buffered < kMaxBufferedBytes) {
      break;  // Buffer has space, proceed
    }

    // Buffer is full, wait a bit for it to drain
    std::this_thread::sleep_for(std::chrono::milliseconds(kBufferCheckIntervalMs));
  }
}

void Sender::SendFile(size_t file_bytes) {
  using clock = std::chrono::steady_clock;
  if (file_bytes == 0) {
    return;
  }

  const auto now = clock::now();
  if (!flow_start_time_initialized_) {
    flow_start_time_ = now;
    flow_start_time_initialized_ = true;
  }

  const uint64_t sequence = next_sequence_++;
  const size_t header_bytes = kFileChunkHeaderSize;
  size_t max_message = conductor_->MaxSctpMessageSize(
      static_cast<Conductor::TrafficKind>(kind_));
  size_t max_chunk_payload = file_bytes;
  if (max_message != 0) {
    if (max_message <= header_bytes + 1) {
      RTC_LOG(LS_WARNING) << "[SCTP][FILE][Sender] Data channel max message size "
                          << max_message
                          << " too small for header overhead; dropping send.";
      return;
    }
    max_chunk_payload = max_message - header_bytes - 1;
  }
  if (max_chunk_payload == 0) {
    max_chunk_payload = file_bytes;
  }

  size_t chunk_count = file_bytes / max_chunk_payload;
  if (file_bytes % max_chunk_payload != 0) {
    ++chunk_count;
  }

  Sender::LogEntry last_entry;
  PayloadMetadata last_metadata;
  size_t chunks_sent = 0;
  size_t bytes_sent = 0;
  
  for (size_t chunk_index = 0; chunk_index < chunk_count && running_.load();
       ++chunk_index) {

    // CRITICAL: Check buffer before sending each chunk
    auto kind_enum = static_cast<Conductor::TrafficKind>(kind_);
    WaitForBufferSpace();
    
    const auto chunk_time = clock::now();
    const uint64_t send_time_ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(
            chunk_time - flow_start_time_)
            .count();
    const size_t remaining_bytes = file_bytes - bytes_sent;
    const size_t chunk_bytes =
        std::min(max_chunk_payload, remaining_bytes);

    PayloadMetadata metadata;
    metadata.sequence = sequence;
    metadata.send_time_ms = send_time_ms;
    metadata.file_bytes = file_bytes;
    metadata.chunk_bytes = chunk_bytes;
    metadata.chunk_index = static_cast<uint32_t>(chunk_index);
    metadata.chunk_count = static_cast<uint32_t>(chunk_count);

    std::vector<uint8_t> payload = BuildChunkPayload(metadata);
    
    // Send the chunk
    conductor_->SendPayload(kind_enum, absl::Span<const uint8_t>(payload));
    
    last_entry = LogSendEvent(metadata);
    last_metadata = metadata;
    ++chunks_sent;
    bytes_sent += chunk_bytes;
    
    // Optional: Add a small delay between chunks for very large files
    // This helps prevent overwhelming the receiver even with buffer checks
    if (chunk_count > 100 && chunk_index % 10 == 0) {
      std::this_thread::sleep_for(std::chrono::microseconds(100));
    }
  }

  if (chunks_sent == 0) {
    return;
  }

  std::ostringstream oss;
  oss << "[SCTP][FILE][Sender] kind=" << kind_
      << " sequence=" << sequence
      << " file_bytes=" << file_bytes
      << " chunks_sent=" << chunks_sent << "/" << chunk_count
      << " total_data_bytes=" << last_entry.total_data_bytes
      << " last_send_time_ms=" << last_metadata.send_time_ms;
  std::cout << oss.str() << std::endl;
}

std::vector<uint8_t> Sender::BuildChunkPayload(const PayloadMetadata& metadata) {
  std::vector<uint8_t> payload(kFileChunkHeaderSize + metadata.chunk_bytes, 0);
  FileChunkHeader header;
  header.sequence = metadata.sequence;
  header.send_time_ms = metadata.send_time_ms;
  header.file_size_bytes = metadata.file_bytes;
  header.chunk_size_bytes = metadata.chunk_bytes;
  header.chunk_index = metadata.chunk_index;
  header.chunk_count = metadata.chunk_count;
  WriteFileChunkHeader(payload.data(), header);
  return payload;
}

Sender::LogEntry Sender::LogSendEvent(const PayloadMetadata& metadata) {
  using clock = std::chrono::steady_clock;
  double timestamp_ms = 0.0;
  uint64_t total_bytes = 0;
  {
    std::lock_guard<std::mutex> lock(log_mutex_);
    const auto now = clock::now();
    if (!log_started_) {
      log_start_time_ = now;
      log_started_ = true;
    }
    timestamp_ms =
        std::chrono::duration<double, std::milli>(now - log_start_time_).count();
    total_data_bytes_sent_ += metadata.chunk_bytes;
    total_bytes = total_data_bytes_sent_;
    if (csv_log_.is_open()) {
      csv_log_ << std::fixed << std::setprecision(3) << timestamp_ms << ","
               << metadata.chunk_bytes << "," << total_bytes << ","
               << metadata.file_bytes << "," << metadata.sequence << ","
               << metadata.chunk_index << "," << metadata.chunk_count << ","
               << metadata.send_time_ms << "\n";
      csv_log_.flush();
    }
  }

  return {timestamp_ms, total_bytes};
}

std::string Sender::MakeLogPath() const {
  std::ostringstream oss;
  oss << "sctp_file_sender_kind" << kind_;
  if (mode_ == Mode::kPeriodic) {
    oss << "_periodic";
  } else {
    oss << "_custom";
  }
  oss << ".csv";
  return oss.str();
}

}  // namespace sctp::file