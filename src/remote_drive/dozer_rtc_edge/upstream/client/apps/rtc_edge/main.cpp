#include "edge_application.h"
#include "edge_options.h"

#include "rtc_logging/rtc_logging.h"
#include "rtc_runtime/process_runtime.h"

#include <exception>
#include <string>
#include <vector>
#include <rclcpp/rclcpp.hpp>

int main(int argc, char** argv) {
  rtc_runtime::InstallSignalHandlers();

  try {
    rclcpp::init(argc, argv, rclcpp::InitOptions(), rclcpp::SignalHandlerOptions::None);
    auto arguments = rclcpp::remove_ros_arguments(argc, argv);
    std::vector<char*> application_arguments;
    for (auto& argument : arguments) {
      application_arguments.push_back(argument.data());
    }
    const rtc_edge_app::EdgeOptions options =
        rtc_edge_app::ParseEdgeArgs(static_cast<int>(application_arguments.size()), application_arguments.data());

    std::string logging_error;
    if (!rtc_logging::InitializeLogging(
            "rtc_edge", &logging_error)) {
      rtc_logging::LogError(
          std::string("Failed to initialize application logging: ") +
          logging_error);
      return 1;
    }

    rtc_logging::LogInfo(
        std::string("RTC SDK log directory: ") + rclcpp::get_logging_directory().string());
    return rtc_edge_app::RunEdgeApplication(options);
  } catch (const std::exception& ex) {
    rtc_logging::LogError(
        std::string("rtc_edge failed: ") + ex.what());
    return 1;
  }
}
