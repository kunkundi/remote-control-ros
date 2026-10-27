#include "rtc_logging/rtc_logging.h"

#include <stdexcept>
#include <rclcpp/rclcpp.hpp>

namespace rtc_logging {
namespace {

class RosLogging {
 public:
  RosLogging() : context_(rclcpp::contexts::get_global_default_context()) {
    if (!context_->is_valid()) {
      context_->init(0, nullptr);
      owns_context_ = true;
    }
    rclcpp::NodeOptions options;
    options.context(context_);
    node_ = std::make_shared<rclcpp::Node>("rtc_edge", options);
  }

  ~RosLogging() {
    node_.reset();
    if (owns_context_ && context_->is_valid()) {
      context_->shutdown("RTC logging stopped");
    }
  }

  rclcpp::Logger logger() const { return node_->get_logger(); }

 private:
  rclcpp::Context::SharedPtr context_;
  rclcpp::Node::SharedPtr node_;
  bool owns_context_ = false;
};

RosLogging& Logging() {
  static RosLogging logging;
  return logging;
}

}

bool InitializeLogging(const std::string& application_name,
                       std::string* error_message) {
  try {
    if (application_name.empty()) {
      throw std::invalid_argument("Application name must not be empty");
    }
    Logging();
    if (error_message) { error_message->clear(); }
    return true;
  } catch (const std::exception& error) {
    if (error_message) { *error_message = error.what(); }
    return false;
  }
}

void FlushLogs() {}

void LogInfo(const std::string& message) {
  RCLCPP_INFO(Logging().logger(), "%s", message.c_str());
}

void LogError(const std::string& message) {
  RCLCPP_ERROR(Logging().logger(), "%s", message.c_str());
}

}