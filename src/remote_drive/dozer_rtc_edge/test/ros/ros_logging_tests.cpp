#include "rtc_logging/rtc_logging.h"
#include "dozer_rtc_edge/ros_sdk_config.h"
#include <sys/stat.h>

#include <chrono>
#include <stdexcept>
#include <string>
#include <thread>
#include <rclcpp/rclcpp.hpp>
#include <rcl_interfaces/msg/log.hpp>

int main(int argc, char** argv) {
  const bool preinitialized = argc > 1 && std::string(argv[1]) == "--preinitialized";
  if (preinitialized) {
    rclcpp::init(0, nullptr);
  }
  std::string error;
    if (!rtc_logging::InitializeLogging("rtc_edge", &error) ||
      !rtc_logging::InitializeLogging("rtc_edge", &error)) {
    throw std::runtime_error(error);
  }
  auto observer = std::make_shared<rclcpp::Node>("rtc_logging_observer");
  std::string temporary_path;
  std::ifstream original_file("rtc.cfg");
  const auto original = nlohmann::json::parse(original_file);
  {
    rtc_edge::RosSdkConfig config("rtc.cfg");
    temporary_path = config.path();
    std::ifstream generated_file(temporary_path);
    const auto generated = nlohmann::json::parse(generated_file);
    auto expected = original;
    expected["log_path"] = rclcpp::get_logging_directory().string();
    struct stat metadata {};
    if (generated != expected || stat(temporary_path.c_str(), &metadata) != 0 ||
        (metadata.st_mode & 0777) != 0600) {
      throw std::runtime_error("SDK ROS log path or private config permissions failed");
    }
  }
  std::ifstream unchanged_file("rtc.cfg");
  if (std::filesystem::exists(temporary_path) ||
      nlohmann::json::parse(unchanged_file) != original) {
    throw std::runtime_error("SDK temporary config cleanup or source preservation failed");
  }
  bool received_info = false;
  bool received_error = false;
  bool received_filtered = false;
  auto subscription = observer->create_subscription<rcl_interfaces::msg::Log>(
      "/rosout", rclcpp::RosoutQoS(),
      [&](rcl_interfaces::msg::Log::ConstSharedPtr message) {
        if (message->name != "rtc_edge") { return; }
        received_info |= message->msg == "rtc_logging_info_probe" &&
                         message->level == rcl_interfaces::msg::Log::INFO;
        received_error |= message->msg == "rtc_logging_error_probe" &&
                          message->level == rcl_interfaces::msg::Log::ERROR;
        received_filtered |= message->msg == "rtc_logging_filtered_probe";
      });
  rclcpp::executors::SingleThreadedExecutor executor;
  executor.add_node(observer);
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
  while ((!received_info || !received_error) && std::chrono::steady_clock::now() < deadline) {
    rtc_logging::LogInfo("rtc_logging_info_probe");
    rclcpp::get_logger("rtc_edge").set_level(rclcpp::Logger::Level::Error);
    rtc_logging::LogInfo("rtc_logging_filtered_probe");
    rtc_logging::LogError("rtc_logging_error_probe");
    rclcpp::get_logger("rtc_edge").set_level(rclcpp::Logger::Level::Info);
    executor.spin_some();
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }
  rtc_logging::FlushLogs();
  if (!received_info || !received_error || received_filtered) {
    throw std::runtime_error("ROS log publication or level filtering failed");
  }
  if (!rclcpp::ok()) {
    throw std::runtime_error("Logging shut down the shared context");
  }
  return 0;
}