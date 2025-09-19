#include "examples/peerconnection/client/sctp_traffic/file/file_sender.h"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <fstream>
#include <sstream>
#include <thread>

#include "absl/types/span.h"
#include "examples/peerconnection/client/conductor.h"

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
}

void Sender::RunPeriodic() {
  std::vector<uint8_t> payload(file_size_, 0);
  while (running_.load()) {
    if (!conductor_->IsFlowOpen(static_cast<Conductor::TrafficKind>(kind_))) {
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
      continue;
    }
    conductor_->SendPayload(
        static_cast<Conductor::TrafficKind>(kind_),
        absl::Span<const uint8_t>(payload));
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
    std::vector<uint8_t> payload(ev.second, 0);
    if (conductor_->IsFlowOpen(static_cast<Conductor::TrafficKind>(kind_))) {
      conductor_->SendPayload(
          static_cast<Conductor::TrafficKind>(kind_),
          absl::Span<const uint8_t>(payload));
    } else {
      // Channel closed right at the send time; skip or busy-wait here if preferred.
      // (Current policy: skip the send if the flow isn't open at due time.)
    }
  }
}

}  // namespace sctp::file
