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
  if (s.empty()) return false;

  size_t i = 0, j = s.size();
  while (i < j && std::isspace(static_cast<unsigned char>(s[i]))) ++i;
  while (j > i && std::isspace(static_cast<unsigned char>(s[j-1]))) --j;
  if (i >= j) return false;

  errno = 0;
  char* end = nullptr;
  long v = std::strtol(s.c_str() + i, &end, 10);

  if (errno != 0) return false;
  if (end == (s.c_str() + i)) return false;
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
  LoadTrace(trace_path);
}

Sender::~Sender() { 
  Stop(); 
}

void Sender::EnableHighSpeedMode(bool enable) {
  high_speed_mode_ = enable;
  if (enable) {
    std::cout << "[SCTP][FILE][Sender] Ultra high-speed mode enabled - "
              << "max_batch=" << Config::MAX_BATCH_SIZE
              << " buffer_threshold=" << (Config::MAX_BUFFER_THRESHOLD / (1024*1024)) << "MB"
              << std::endl;
  }
}

void Sender::Start(Conductor& c) {
  conductor_ = &c;
  running_.store(true);
  
  // Initialize logging
  {
    std::lock_guard<std::mutex> lock(log_mutex_);
    total_data_bytes_sent_ = 0;
    log_started_ = false;
    log_path_ = MakeLogPath();
    csv_log_.open(log_path_, std::ios::out | std::ios::trunc);
    if (csv_log_.is_open()) {
      csv_log_ << "timestamp_ms,chunk_bytes,total_data_bytes,file_bytes,sequence,chunk_index,chunk_count,send_time_ms\n";
      csv_log_.flush();
    }
  }
  
  std::cout << "[SCTP][FILE][Sender] Starting sender kind=" << kind_ 
            << " mode=" << (mode_ == Mode::kPeriodic ? "periodic" : "custom")
            << " high_speed=" << high_speed_mode_ << std::endl;
  
  if (mode_ == Mode::kPeriodic) {
    worker_ = std::thread([this]() { RunPeriodic(); });
  } else {
    worker_ = std::thread([this]() { RunCustom(); });
  }
}

void Sender::Stop() {
  running_.store(false);
  if (worker_.joinable()) {
    worker_.join();
  }
  
  uint64_t total_bytes = 0;
  {
    std::lock_guard<std::mutex> lock(log_mutex_);
    if (csv_log_.is_open()) {
      csv_log_.flush();
      csv_log_.close();
    }
    total_bytes = total_data_bytes_sent_;
  }
  
  std::cout << "[SCTP][FILE][Sender] Stopped. Total bytes sent: " 
            << total_bytes << std::endl;
}

void Sender::RunPeriodic() {
  while (running_.load()) {
    if (!IsFlowReady()) {
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
      continue;
    }
    
    SendFileBatched(file_size_);
    
    if (periodicity_ms_ > 0) {
      std::this_thread::sleep_for(std::chrono::milliseconds(periodicity_ms_));
    }
  }
}

void Sender::RunCustom() {
  using clock = std::chrono::steady_clock;

  // Wait for channel to open
  while (running_.load() && !IsFlowReady()) {
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  if (!running_.load()) return;

  const auto t0 = clock::now();

  for (const auto& ev : custom_events_) {
    if (!running_.load()) break;

    const auto due = t0 + std::chrono::milliseconds(ev.first);
    
    // Precise timing loop
    while (running_.load() && clock::now() < due) {
      std::this_thread::sleep_for(std::chrono::microseconds(100));
    }
    if (!running_.load()) break;

    if (IsFlowReady()) {
      SendFileBatched(ev.second);
    }
  }
}

void Sender::LoadTrace(const std::string& path) {
  std::ifstream file(path);
  if (!file.is_open()) return;

  auto trim = [](std::string& s) {
    auto issp = [](unsigned char c){ return std::isspace(c); };
    s.erase(s.begin(), std::find_if(s.begin(), s.end(), [&](unsigned char c){ return !issp(c); }));
    s.erase(std::find_if(s.rbegin(), s.rend(), [&](unsigned char c){ return !issp(c); }).base(), s.end());
  };

  std::string line;
  bool first = true;
  while (std::getline(file, line)) {
    if (line.empty()) continue;

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

    custom_events_.emplace_back(t, sz);
  }

  std::sort(custom_events_.begin(), custom_events_.end(),
            [](const auto& a, const auto& b){ return a.first < b.first; });
}

bool Sender::IsFlowReady() {
  return conductor_ && 
         conductor_->IsFlowOpen(static_cast<Conductor::TrafficKind>(kind_));
}

bool Sender::WaitForBufferSpace() {
  if (!conductor_) return false;
  
  const auto kind = static_cast<Conductor::TrafficKind>(kind_);
  
  // Quick check first
  if (!conductor_->IsFlowOpen(kind)) {
    return false;
  }
  
  uint64_t buffered = conductor_->BufferedAmount(kind);
  
  // If buffer is reasonable, proceed immediately
  if (buffered < Config::BUFFER_CHECK_THRESHOLD) {
    return true;
  }
  
  // Wait for buffer to drain with minimal overhead
  int wait_count = 0;
  while (running_.load() && buffered >= Config::MAX_BUFFER_THRESHOLD) {
    if (!conductor_->IsFlowOpen(kind)) {
      return false;
    }
    
    // Progressive backoff: yield first, then short sleeps
    if (wait_count < 10) {
      std::this_thread::yield();
    } else if (wait_count < 100) {
      std::this_thread::sleep_for(std::chrono::microseconds(10));
    } else {
      std::this_thread::sleep_for(std::chrono::microseconds(100));
    }
    
    wait_count++;
    buffered = conductor_->BufferedAmount(kind);
  }
  
  return running_.load();
}

void Sender::SendFileBatched(size_t file_bytes) {
  using clock = std::chrono::steady_clock;
  
  if (file_bytes == 0) return;

  const auto now = clock::now();
  if (!flow_start_time_initialized_) {
    flow_start_time_ = now;
    flow_start_time_initialized_ = true;
  }

  const uint64_t sequence = next_sequence_++;
  const auto file_send_start_time = now;
  
  // Calculate chunk parameters
  size_t max_message = conductor_->MaxSctpMessageSize(
      static_cast<Conductor::TrafficKind>(kind_));
  
  size_t max_chunk_payload = file_bytes;
  if (max_message > kFileChunkHeaderSize + 1) {
    max_chunk_payload = max_message - kFileChunkHeaderSize - 1;
  }
  if (max_chunk_payload == 0) {
    max_chunk_payload = 65536; // Default 64KB chunks
  }
  
  // Ensure chunk size is reasonable for high throughput
  max_chunk_payload = std::min(max_chunk_payload, static_cast<size_t>(1024 * 1024)); // Max 1MB per chunk

  size_t chunk_count = (file_bytes + max_chunk_payload - 1) / max_chunk_payload;

  std::cout << "[SCTP][FILE][Sender] HIGH-SPEED transmission: "
            << "seq=" << sequence
            << " file_bytes=" << file_bytes
            << " chunks=" << chunk_count
            << " chunk_size=" << max_chunk_payload << std::endl;

  const auto kind_enum = static_cast<Conductor::TrafficKind>(kind_);
  
  // Pre-allocate buffers for maximum performance
  std::vector<uint8_t> payload_buffer;
  payload_buffer.reserve(kFileChunkHeaderSize + max_chunk_payload);
  
  size_t chunks_sent = 0;
  size_t bytes_sent = 0;

  for (size_t chunk_index = 0; chunk_index < chunk_count && running_.load(); ) {
    
    // Check buffer space periodically, not every chunk
    if (chunk_index % Config::BUFFER_CHECK_INTERVAL == 0) {
      if (!WaitForBufferSpace()) {
        std::cout << "[SCTP][FILE][Sender] Buffer wait failed, stopping transmission" << std::endl;
        break;
      }
    }
    
    // Calculate dynamic batch size based on remaining chunks
    size_t remaining_chunks = chunk_count - chunk_index;
    size_t current_batch_size = std::min({
        Config::MAX_BATCH_SIZE,
        remaining_chunks,
        static_cast<size_t>(high_speed_mode_ ? Config::MAX_BATCH_SIZE : Config::MIN_BATCH_SIZE)
    });
    
    size_t batch_end = chunk_index + current_batch_size;
    
    // Send entire batch without interruption for maximum throughput
    for (size_t i = chunk_index; i < batch_end && running_.load(); ++i) {
      const size_t remaining_bytes = file_bytes - bytes_sent;
      const size_t chunk_bytes = std::min(max_chunk_payload, remaining_bytes);

      const auto chunk_time = clock::now();
      const uint64_t send_time_ms =
          std::chrono::duration_cast<std::chrono::milliseconds>(
              chunk_time - flow_start_time_).count();

      // Build payload with pre-allocated buffer
      payload_buffer.clear();
      payload_buffer.resize(kFileChunkHeaderSize + chunk_bytes, 0);
      
      FileChunkHeader header;
      header.sequence = sequence;
      header.send_time_ms = send_time_ms;
      header.file_size_bytes = file_bytes;
      header.chunk_size_bytes = chunk_bytes;
      header.chunk_index = static_cast<uint32_t>(i);
      header.chunk_count = static_cast<uint32_t>(chunk_count);
      
      WriteFileChunkHeader(payload_buffer.data(), header);
      
      // Send immediately
      conductor_->SendPayload(kind_enum, absl::Span<const uint8_t>(payload_buffer));
      
      // Reduced logging for performance
      if (i % Config::LOG_INTERVAL == 0 || chunk_count < Config::LOG_INTERVAL) {
        PayloadMetadata metadata;
        metadata.sequence = sequence;
        metadata.send_time_ms = send_time_ms;
        metadata.file_bytes = file_bytes;
        metadata.chunk_bytes = chunk_bytes;
        metadata.chunk_index = static_cast<uint32_t>(i);
        metadata.chunk_count = static_cast<uint32_t>(chunk_count);
        LogSendEvent(metadata);
      }
      
      ++chunks_sent;
      bytes_sent += chunk_bytes;
    }

    chunk_index = batch_end;
    
    // Minimal yield only for very large batches
    if (current_batch_size >= 1000) {
      std::this_thread::yield();
    }
  }

  const auto file_send_end_time = clock::now();
  const uint64_t total_duration_ms =
      std::chrono::duration_cast<std::chrono::milliseconds>(
          file_send_end_time - file_send_start_time).count();

  double effective_rate_mbps = 0.0;
  if (total_duration_ms > 0) {
    effective_rate_mbps = (bytes_sent * 8.0 / 1000000.0) / (total_duration_ms / 1000.0);
  }

  std::cout << "[SCTP][FILE][Sender] COMPLETED: "
            << "seq=" << sequence  
            << " chunks=" << chunks_sent << "/" << chunk_count
            << " bytes=" << bytes_sent
            << " duration_ms=" << total_duration_ms
            << " rate_mbps=" << std::fixed << std::setprecision(2) << effective_rate_mbps
            << std::endl;
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

void Sender::LogSendEvent(const PayloadMetadata& metadata) {
  using clock = std::chrono::steady_clock;
  
  std::lock_guard<std::mutex> lock(log_mutex_);
  
  const auto now = clock::now();
  if (!log_started_) {
    log_start_time_ = now;
    log_started_ = true;
  }
  
  double timestamp_ms =
      std::chrono::duration<double, std::milli>(now - log_start_time_).count();
  
  total_data_bytes_sent_ += metadata.chunk_bytes;
  
  if (csv_log_.is_open()) {
    csv_log_ << std::fixed << std::setprecision(3) << timestamp_ms << ","
             << metadata.chunk_bytes << "," << total_data_bytes_sent_ << ","
             << metadata.file_bytes << "," << metadata.sequence << ","
             << metadata.chunk_index << "," << metadata.chunk_count << ","
             << metadata.send_time_ms << "\n";
    
    // Flush less frequently for performance
    if (metadata.chunk_index % 100 == 0) {
      csv_log_.flush();
    }
  }
}

std::string Sender::MakeLogPath() const {
  std::ostringstream oss;
  oss << "sctp_file_sender_kind" << kind_;
  if (mode_ == Mode::kPeriodic) {
    oss << "_periodic";
  } else {
    oss << "_custom";
  }
  if (high_speed_mode_) {
    oss << "_highspeed";
  }
  oss << ".csv";
  return oss.str();
}

}  // namespace sctp::file