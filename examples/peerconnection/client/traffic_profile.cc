#include "examples/peerconnection/client/traffic_profile.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>
#include <vector>

std::vector<TrafficProfile> LoadProfiles(const std::string& path) {
  std::vector<TrafficProfile> profiles;
  std::ifstream file(path);
  if (!file.is_open()) {
    return profiles;
  }

  auto trim = [](std::string& value) {
    const auto is_space = [](unsigned char c) { return std::isspace(c); };
    value.erase(value.begin(),
                std::find_if(value.begin(), value.end(),
                             [&](unsigned char c) { return !is_space(c); }));
    value.erase(std::find_if(value.rbegin(), value.rend(),
                             [&](unsigned char c) { return !is_space(c); })
                    .base(),
                value.end());
  };

  std::string line;
  bool first_line = true;
  while (std::getline(file, line)) {
    if (line.empty()) {
      continue;
    }
    // Skip header if present.
    if (first_line && line.find("Traffic") != std::string::npos) {
      first_line = false;
      continue;
    }
    first_line = false;
    std::istringstream ss(line);
    std::string item;
    TrafficProfile profile;
    std::vector<std::string> columns;
    while (std::getline(ss, item, ',')) {
      trim(item);
      columns.push_back(item);
    }

    auto get_column = [&](size_t index) -> const std::string* {
      if (index >= columns.size()) {
        return nullptr;
      }
      return &columns[index];
    };

    if (const std::string* value = get_column(0)) profile.traffic_name = *value;
    if (const std::string* value = get_column(1)) profile.protocol = *value;
    if (const std::string* value = get_column(2)) profile.pattern = *value;
    if (const std::string* value = get_column(3); value && !value->empty())
      profile.file_size = std::stoi(*value);
    if (const std::string* value = get_column(4); value && !value->empty())
      profile.periodicity = std::stoi(*value);
    if (const std::string* value = get_column(5)) profile.custom_trace = *value;
    if (const std::string* value = get_column(6); value && !value->empty())
      profile.max_bitrate = std::stoi(*value);
    if (const std::string* value = get_column(7); value && !value->empty())
      profile.frame_rate = std::stoi(*value);
    if (const std::string* value = get_column(8))
      profile.video_file_name = *value;
    if (const std::string* value = get_column(9); value && !value->empty())
      profile.slo_ms = std::stoi(*value);

    profiles.push_back(std::move(profile));
  }
  return profiles;
}
