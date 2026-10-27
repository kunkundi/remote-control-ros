#include "dozer_rtc_edge/ros_context.h"

#include <mutex>

namespace rtc_edge {
namespace {

class RosRuntime {
 public:
  std::shared_ptr<rclcpp::Context> Acquire() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!context_->is_valid()) {
      context_->init(0, nullptr);
      owns_initialization_ = true;
    }
    return context_;
  }

  ~RosRuntime() {
    if (owns_initialization_ && context_->is_valid()) {
      context_->shutdown("RTC inputs stopped");
    }
  }

 private:
  std::shared_ptr<rclcpp::Context> context_ = rclcpp::contexts::get_global_default_context();
  std::mutex mutex_;
  bool owns_initialization_ = false;
};

}

std::shared_ptr<rclcpp::Context> AcquireRosContext() {
  static RosRuntime runtime;
  return runtime.Acquire();
}

}