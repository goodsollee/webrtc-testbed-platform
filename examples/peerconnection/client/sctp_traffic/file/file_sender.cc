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
  
  std::cout << "[SCTP][FILE][Sender] High buffer detected: " << buffered 
            << " bytes, waiting for drain..." << std::endl;
  
  // Wait for buffer to drain with exponential backoff
  auto start_time = std::chrono::steady_clock::now();
  const auto max_wait_time = std::chrono::seconds(30); // Maximum wait time
  
  int wait_count = 0;
  auto wait_duration = std::chrono::microseconds(100);
  const auto max_wait_duration = std::chrono::microseconds(10000); // 10ms in microseconds
  
  while (running_.load() && buffered >= Config::MAX_BUFFER_THRESHOLD) {
    // Check if we've exceeded maximum wait time
    auto elapsed = std::chrono::steady_clock::now() - start_time;
    if (elapsed > max_wait_time) {
      std::cout << "[SCTP][FILE][Sender] Buffer wait timeout after " 
                << std::chrono::duration_cast<std::chrono::seconds>(elapsed).count() 
                << " seconds" << std::endl;
      return false;
    }
    
    if (!conductor_->IsFlowOpen(kind)) {
      std::cout << "[SCTP][FILE][Sender] Flow closed during buffer wait" << std::endl;
      return false;
    }
    
    // Progressive backoff with cap
    std::this_thread::sleep_for(wait_duration);
    if (wait_duration < max_wait_duration) {
      wait_duration = std::min(wait_duration * 2, max_wait_duration);
    }
    
    wait_count++;
    
    // Log progress every second
    if (wait_count % 100 == 0) {
      buffered = conductor_->BufferedAmount(kind);
      std::cout << "[SCTP][FILE][Sender] Still waiting, buffer: " << buffered 
                << " bytes (target: < " << Config::MAX_BUFFER_THRESHOLD << ")" << std::endl;
    } else {
      buffered = conductor_->BufferedAmount(kind);
    }
  }
  
  if (buffered < Config::MAX_BUFFER_THRESHOLD) {
    std::cout << "[SCTP][FILE][Sender] Buffer drained, proceeding. Final buffer: " 
              << buffered << " bytes" << std::endl;
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
  
  // Use large chunks for maximum throughput efficiency
  size_t max_message = conductor_->MaxSctpMessageSize(
      static_cast<Conductor::TrafficKind>(kind_));
  
  size_t max_chunk_payload = file_bytes;
  if (max_message > kFileChunkHeaderSize + 1) {
    max_chunk_payload = max_message - kFileChunkHeaderSize - 1;
  }
  if (max_chunk_payload == 0) {
    max_chunk_payload = 256 * 1024; // 256KB fallback - much larger than 32KB
  }
  
  // Use large chunks but cap at reasonable size for memory
  max_chunk_payload = std::min(max_chunk_payload, static_cast<size_t>(512 * 1024)); // 512KB max
  size_t chunk_count = (file_bytes + max_chunk_payload - 1) / max_chunk_payload;

  std::cout << "[SCTP][FILE][Sender] HIGH-THROUGHPUT transmission: "
            << "seq=" << sequence
            << " file_bytes=" << file_bytes
            << " chunks=" << chunk_count
            << " chunk_size=" << max_chunk_payload << std::endl;

  const auto kind_enum = static_cast<Conductor::TrafficKind>(kind_);
  
  // Pre-allocate buffers
  std::vector<uint8_t> payload_buffer;
  payload_buffer.reserve(kFileChunkHeaderSize + max_chunk_payload);
  
  size_t chunks_sent = 0;
  size_t bytes_sent = 0;
  
  // Adaptive flow control based on observed receiver capacity
  // Start conservative and adapt based on actual performance
  uint64_t adaptive_buffer_limit = 256 * 1024;  // Start at 256KB
  const uint64_t min_buffer_limit = 128 * 1024;  // Never go below 128KB
  const uint64_t max_buffer_limit = 2 * 1024 * 1024;  // Never exceed 2MB
  
  // Performance tracking for adaptation
  auto last_successful_batch = clock::now();
  size_t consecutive_successes = 0;
  size_t consecutive_waits = 0;
  
  for (size_t chunk_index = 0; chunk_index < chunk_count && running_.load(); ++chunk_index) {
    
    // Check flow status
    if (!conductor_->IsFlowOpen(kind_enum)) {
      std::cout << "[SCTP][FILE][Sender] Flow closed, aborting transmission" << std::endl;
      break;
    }
    
    // Get current buffer level
    uint64_t current_buffer = conductor_->BufferedAmount(kind_enum);
    
    // Adaptive buffer management
    if (current_buffer >= adaptive_buffer_limit) {
      consecutive_waits++;
      consecutive_successes = 0;
      
      // If we're waiting too often, increase the buffer limit (receiver can handle more)
      if (consecutive_waits >= 5 && adaptive_buffer_limit < max_buffer_limit) {
        adaptive_buffer_limit = std::min(adaptive_buffer_limit * 2, max_buffer_limit);
        std::cout << "[SCTP][FILE][Sender] Increased buffer limit to " << adaptive_buffer_limit << std::endl;
        consecutive_waits = 0;
      }
      
      // Calculate proportional wait time
      double buffer_ratio = static_cast<double>(current_buffer) / adaptive_buffer_limit;
      auto wait_time = std::chrono::microseconds(static_cast<int>(500 * buffer_ratio));
      
      std::cout << "[SCTP][FILE][Sender] Buffer=" << current_buffer 
                << "/" << adaptive_buffer_limit 
                << ", waiting " << wait_time.count() << "us" << std::endl;
      
      std::this_thread::sleep_for(wait_time);
      --chunk_index;  // Retry this chunk
      continue;
    }
    
    // Build and send chunk
    const size_t chunk_start_byte = chunk_index * max_chunk_payload;
    const size_t remaining_bytes = file_bytes - chunk_start_byte;
    const size_t chunk_bytes = std::min(max_chunk_payload, remaining_bytes);

    const auto chunk_time = clock::now();
    const uint64_t send_time_ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(
            chunk_time - flow_start_time_).count();

    // Build payload
    payload_buffer.clear();
    payload_buffer.resize(kFileChunkHeaderSize + chunk_bytes, 0);
    
    FileChunkHeader header;
    header.sequence = sequence;
    header.send_time_ms = send_time_ms;
    header.file_size_bytes = file_bytes;
    header.chunk_size_bytes = chunk_bytes;
    header.chunk_index = static_cast<uint32_t>(chunk_index);
    header.chunk_count = static_cast<uint32_t>(chunk_count);
    
    WriteFileChunkHeader(payload_buffer.data(), header);
    
    // Attempt to send
    if (conductor_->SendPayload(kind_enum, absl::Span<const uint8_t>(payload_buffer))) {
      // Success
      ++chunks_sent;
      bytes_sent += chunk_bytes;
      consecutive_successes++;
      consecutive_waits = 0;
      last_successful_batch = chunk_time;
      
      // If we're consistently successful with low buffer usage, we can be more aggressive
      if (consecutive_successes >= 10 && current_buffer < adaptive_buffer_limit / 4) {
        if (adaptive_buffer_limit > min_buffer_limit) {
          // Don't increase too aggressively - receiver might not keep up
          adaptive_buffer_limit = std::max(adaptive_buffer_limit * 9 / 10, min_buffer_limit);
          std::cout << "[SCTP][FILE][Sender] Decreased buffer limit to " << adaptive_buffer_limit 
                    << " for higher throughput" << std::endl;
        }
        consecutive_successes = 0;
      }
      
      // Log progress
      if (chunk_index % std::max(static_cast<size_t>(1), chunk_count / 20) == 0 || chunk_index == chunk_count - 1) {
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            chunk_time - file_send_start_time);
        double rate_mbps = 0.0;
        if (elapsed.count() > 0) {
          rate_mbps = (bytes_sent * 8.0 / 1000000.0) / (elapsed.count() / 1000.0);
        }
        
        std::cout << "[SCTP][FILE][Sender] Progress: " << chunks_sent << "/" << chunk_count 
                  << " (" << std::fixed << std::setprecision(1) 
                  << (100.0 * chunks_sent / chunk_count) << "%) "
                  << "rate=" << std::setprecision(1) << rate_mbps << "Mbps "
                  << "buffer=" << current_buffer 
                  << " limit=" << adaptive_buffer_limit << std::endl;
      }
      
      // Minimal pacing - only when buffer is getting high
      if (current_buffer > adaptive_buffer_limit / 2) {
        std::this_thread::sleep_for(std::chrono::microseconds(100));
      }
      
    } else {
      // Send failed - this indicates the buffer is really full
      std::cout << "[SCTP][FILE][Sender] Send failed for chunk " << chunk_index 
                << ", buffer=" << current_buffer << std::endl;
      
      // Reduce buffer limit since we hit resistance
      adaptive_buffer_limit = std::max(adaptive_buffer_limit / 2, min_buffer_limit);
      std::cout << "[SCTP][FILE][Sender] Reduced buffer limit to " << adaptive_buffer_limit << std::endl;
      
      // Wait and retry
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
      --chunk_index;
      continue;
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

  double completion_rate = (100.0 * chunks_sent / chunk_count);
  
  std::cout << "[SCTP][FILE][Sender] COMPLETED: "
            << "seq=" << sequence  
            << " chunks=" << chunks_sent << "/" << chunk_count
            << " (" << std::fixed << std::setprecision(1) << completion_rate << "%) "
            << " bytes=" << bytes_sent << "/" << file_bytes
            << " duration_ms=" << total_duration_ms
            << " rate_mbps=" << std::setprecision(2) << effective_rate_mbps
            << " final_buffer_limit=" << adaptive_buffer_limit << std::endl;
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