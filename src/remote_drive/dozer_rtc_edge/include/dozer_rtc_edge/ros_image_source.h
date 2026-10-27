#pragma once

#include <atomic>
#include <chrono>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include <opencv2/core.hpp>
#include <rclcpp/rclcpp.hpp>
#include <sensor_msgs/msg/image.hpp>

namespace rtc_edge {

cv::Mat ImageToBgr(const sensor_msgs::msg::Image& image);

class RosImageSource {
 public:
  explicit RosImageSource(const std::vector<std::string>& topics);
  ~RosImageSource();
  bool TakeI420(cv::Mat* output);
  uint64_t received() const { return received_.load(); }

 private:
  std::shared_ptr<rclcpp::Context> context_;
  rclcpp::Node::SharedPtr node_;
  std::unique_ptr<rclcpp::executors::SingleThreadedExecutor> executor_;
  std::vector<rclcpp::Subscription<sensor_msgs::msg::Image>::SharedPtr> subscriptions_;
  std::vector<sensor_msgs::msg::Image::ConstSharedPtr> latest_;
  std::mutex mutex_;
  std::thread worker_;
  std::atomic<uint64_t> received_{0};
};

}