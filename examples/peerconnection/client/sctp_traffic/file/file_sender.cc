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
  
  // Reset backpressure state
  current_batch_size_ = backpressure_config_.batch_size;
  current_check_interval_ms_ = backpressure_config_.base_check_interval_ms;
  consecutive_blocks_ = 0;
  
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
    SendFileBatched(file_size_);
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
      SendFileBatched(ev.second);
    }
  }
}

bool Sender::CheckBufferSpaceNonBlocking() {
  if (!conductor_) return false;
  
  const auto kind = static_cast<Conductor::TrafficKind>(kind_);
  if (!conductor_->IsFlowOpen(kind)) return false;
  
  uint64_t buffered = conductor_->BufferedAmount(kind);
  
  // Adaptive parameter adjustment based on current buffer state
  AdaptBackpressureParameters(buffered);
  
  return buffered < backpressure_config_.buffer_threshold;
}

bool Sender::WaitForBufferSpaceAdaptive() {
  if (!conductor_) return false;

  const auto kind = static_cast<Conductor::TrafficKind>(kind_);
  
  while (running_.load()) {
    if (!conductor_->IsFlowOpen(kind)) {
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
      continue;
    }

    uint64_t buffered = conductor_->BufferedAmount(kind);
    AdaptBackpressureParameters(buffered);
    
    // Use hysteresis: wait until buffer drains to target level
    if (buffered < backpressure_config_.buffer_target) {
      consecutive_blocks_ = 0;  // Reset backoff counter
      return true;
    }

    // Exponential backoff when repeatedly blocked
    int sleep_ms = current_check_interval_ms_;
    if (backpressure_config_.use_exponential_backoff && consecutive_blocks_ > 0) {
      sleep_ms = std::min(sleep_ms * (1 << std::min(consecutive_blocks_, 4)), 
                         backpressure_config_.max_check_interval_ms);
    }
    
    ++consecutive_blocks_;
    std::this_thread::sleep_for(std::chrono::milliseconds(sleep_ms));
  }
  
  return false;
}

void Sender::AdaptBackpressureParameters(uint64_t current_buffered) {
  const double buffer_ratio = static_cast<double>(current_buffered) / 
                              backpressure_config_.buffer_threshold;
  
  if (backpressure_config_.adaptive_batching) {
    // Adapt batch size based on buffer state
    if (buffer_ratio < 0.25) {
      // Buffer very low - increase batch size for efficiency
      current_batch_size_ = std::min(backpressure_config_.batch_size * 3, 
                                   static_cast<size_t>(12));
    } else if (buffer_ratio < 0.5) {
      // Buffer low - moderate increase
      current_batch_size_ = std::min(backpressure_config_.batch_size * 2, 
                                   static_cast<size_t>(8));
    } else if (buffer_ratio > 0.8) {
      // Buffer getting full - reduce batch size for responsiveness
      current_batch_size_ = 1;
    } else {
      // Normal range - use default batch size
      current_batch_size_ = backpressure_config_.batch_size;
    }
  }
  
  // Adapt check interval based on buffer fullness
  if (buffer_ratio < 0.3) {
    current_check_interval_ms_ = backpressure_config_.base_check_interval_ms;
  } else {
    // Increase check interval as buffer fills up (less aggressive polling)
    current_check_interval_ms_ = std::min(
      static_cast<int>(backpressure_config_.base_check_interval_ms * (1 + buffer_ratio * 4)),
      backpressure_config_.max_check_interval_ms
    );
  }
}

void Sender::SendFile(size_t file_bytes) {
  // Use the new batched implementation
  SendFileBatched(file_bytes);
}

void Sender::SendFileBatched(size_t file_bytes) {
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
  const auto file_send_start_time = clock::now();
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

  
  // Log start of file sending
  const uint64_t start_time_ms =
      std::chrono::duration_cast<std::chrono::milliseconds>(
          file_send_start_time - flow_start_time_)
          .count();
  
  std::cout << "[SCTP][FILE][Sender] kind=" << kind_
            << " sequence=" << sequence
            << " STARTED sending file_bytes=" << file_bytes
            << " chunk_count=" << chunk_count
            << " start_time_ms=" << start_time_ms << std::endl;

  Sender::LogEntry last_entry;
  PayloadMetadata last_metadata;
  size_t chunks_sent = 0;
  size_t bytes_sent = 0;
  
  for (size_t chunk_index = 0; chunk_index < chunk_count && running_.load();) {
    
    // Check buffer space before sending a batch
    if (!CheckBufferSpaceNonBlocking()) {
      if (!WaitForBufferSpaceAdaptive()) {
        break;  // Stopped or flow closed
      }
    }
    
    // Send a batch of chunks with buffer checking
    size_t batch_end = std::min(chunk_index + current_batch_size_, chunk_count);
    
    for (size_t i = chunk_index; i < batch_end && running_.load(); ++i) {
      // Check buffer every few chunks within the batch to prevent overflow
      if (i > chunk_index && (i - chunk_index) % 8 == 0) {
        if (!CheckBufferSpaceNonBlocking()) {
          // Buffer getting full mid-batch, reduce remaining batch size
          batch_end = i;
          break;
        }
      }
      
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
      metadata.chunk_index = static_cast<uint32_t>(i);
      metadata.chunk_count = static_cast<uint32_t>(chunk_count);

      std::vector<uint8_t> payload = BuildChunkPayload(metadata);
      
      // Send the chunk
      auto kind_enum = static_cast<Conductor::TrafficKind>(kind_);
      conductor_->SendPayload(kind_enum, absl::Span<const uint8_t>(payload));
      
      last_entry = LogSendEvent(metadata);
      last_metadata = metadata;
      ++chunks_sent;
      bytes_sent += chunk_bytes;
      
      // Small pause between chunks to prevent receiver overwhelm
      if (current_batch_size_ > 16) {
        std::this_thread::sleep_for(std::chrono::microseconds(10));
      }
    }
    
    chunk_index = batch_end;
    
    // Small yield between batches to prevent completely monopolizing the thread
    if (current_batch_size_ > 1 && chunk_index < chunk_count) {
      std::this_thread::sleep_for(std::chrono::microseconds(50));
    }
  }

  if (chunks_sent == 0) {
    return;
  }

  // Log completion of file sending with timing
  const auto file_send_end_time = clock::now();
  const uint64_t end_time_ms =
      std::chrono::duration_cast<std::chrono::milliseconds>(
          file_send_end_time - flow_start_time_)
          .count();
  const uint64_t total_send_duration_ms =
      std::chrono::duration_cast<std::chrono::milliseconds>(
          file_send_end_time - file_send_start_time)
          .count();

  std::ostringstream oss;
  oss << "[SCTP][FILE][Sender] kind=" << kind_
      << " sequence=" << sequence
      << " COMPLETED file_bytes=" << file_bytes
      << " chunks_sent=" << chunks_sent << "/" << chunk_count
      << " batch_size=" << current_batch_size_
      << " total_data_bytes=" << last_entry.total_data_bytes
      << " start_time_ms=" << start_time_ms
      << " end_time_ms=" << end_time_ms
      << " duration_ms=" << total_send_duration_ms;
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