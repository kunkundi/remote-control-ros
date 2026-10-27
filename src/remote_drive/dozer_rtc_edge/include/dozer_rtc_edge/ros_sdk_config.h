#pragma once

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>
#include <unistd.h>
#include <nlohmann/json.hpp>
#include <rclcpp/logger.hpp>

namespace rtc_edge {

class RosSdkConfig {
 public:
  explicit RosSdkConfig(const std::string& source_path) {
    std::ifstream source(source_path);
    auto config = nlohmann::json::parse(source);
    const std::string directory = rclcpp::get_logging_directory().string();
    std::filesystem::create_directories(directory);
    config["log_path"] = directory;
    const std::string contents = config.dump();
    const std::string pattern = directory + "/.rtc_edge_config_XXXXXX";
    std::vector<char> filename(pattern.begin(), pattern.end());
    filename.push_back('\0');
    const int descriptor = mkstemp(filename.data());
    if (descriptor < 0) {
      throw std::runtime_error("Failed to create private RTC SDK configuration");
    }
    FILE* file = fdopen(descriptor, "w");
    if (!file) {
      close(descriptor);
      unlink(filename.data());
      throw std::runtime_error("Failed to open RTC SDK configuration");
    }
    const bool written = fwrite(contents.data(), 1, contents.size(), file) == contents.size();
    const bool closed = fclose(file) == 0;
    if (!written || !closed) {
      unlink(filename.data());
      throw std::runtime_error("Failed to write RTC SDK configuration");
    }
    path_ = filename.data();
  }

  ~RosSdkConfig() { unlink(path_.c_str()); }
  RosSdkConfig(const RosSdkConfig&) = delete;
  RosSdkConfig& operator=(const RosSdkConfig&) = delete;
  const std::string& path() const { return path_; }

 private:
  std::string path_;
};

}