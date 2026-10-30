#include "dozer_rtc_edge/ros_image_source.h"
#include "dozer_rtc_edge/ros_context.h"

#include <limits>
#include <stdexcept>
#include <opencv2/imgproc.hpp>

namespace rtc_edge {

cv::Mat ImageToBgr(const sensor_msgs::msg::Image& image) {
  if (image.width == 0 || image.height == 0 || image.width % 2 || image.height % 2 ||
      image.width > static_cast<uint32_t>(std::numeric_limits<int>::max() / 3) ||
      image.height > static_cast<uint32_t>(std::numeric_limits<int>::max()) ||
      image.step < static_cast<uint64_t>(image.width) * 3 ||
      image.data.size() < static_cast<uint64_t>(image.step) * image.height ||
      (image.encoding != "bgr8" && image.encoding != "rgb8")) {
    throw std::invalid_argument("ROS camera requires valid even-sized bgr8/rgb8 images");
  }
  cv::Mat view(static_cast<int>(image.height), static_cast<int>(image.width),
               CV_8UC3, const_cast<uint8_t*>(image.data.data()), image.step);
  cv::Mat result;
  if (image.encoding == "rgb8") {
    cv::cvtColor(view, result, cv::COLOR_RGB2BGR);
  } else {
    result = view.clone();
  }
  return result;
}

RosImageSource::RosImageSource(const std::vector<std::string>& topics) {
  if (topics.empty() || topics.size() > 2) {
    throw std::invalid_argument("Expected one or two ROS camera topics");
  }
  for (const auto& topic : topics) {
    if (topic.empty() || topic.front() != '/' || topic.find("/dev/") == 0) {
      throw std::invalid_argument("Camera input must be an absolute ROS image topic, not a device: " + topic);
    }
  }
  context_ = AcquireRosContext();
  rclcpp::NodeOptions options;
  options.context(context_);
  static std::atomic<uint64_t> instance{0};
  node_ = std::make_shared<rclcpp::Node>("rtc_camera_input_" + std::to_string(instance++), options);
  rclcpp::ExecutorOptions executor_options;
  executor_options.context = context_;
  executor_ = std::make_unique<rclcpp::executors::SingleThreadedExecutor>(executor_options);
  latest_.resize(topics.size());
  for (size_t index = 0; index < topics.size(); ++index) {
    subscriptions_.push_back(node_->create_subscription<sensor_msgs::msg::Image>(
        topics[index], rclcpp::SensorDataQoS().keep_last(1),
        [this, index](sensor_msgs::msg::Image::ConstSharedPtr image) {
          {
            std::lock_guard<std::mutex> lock(mutex_);
            if (stopped_) { return; }
            latest_[index] = std::move(image);
            ++received_;
          }
          frame_ready_.notify_one();
        }));
  }
  executor_->add_node(node_);
  worker_ = std::thread([this] {
    while (!stopped_.load() && context_->is_valid()) {
      executor_->spin_once(std::chrono::milliseconds(100));
    }
  });
}

RosImageSource::~RosImageSource() {
  RequestStop();
  if (worker_.joinable()) { worker_.join(); }
  executor_->remove_node(node_);
}

void RosImageSource::RequestStop() {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    stopped_ = true;
  }
  frame_ready_.notify_all();
  executor_->cancel();
}

bool RosImageSource::TakeI420(cv::Mat* output, std::chrono::milliseconds timeout) {
  std::vector<sensor_msgs::msg::Image::ConstSharedPtr> frames;
  {
    std::unique_lock<std::mutex> lock(mutex_);
    const auto ready = [this] {
      if (stopped_) { return true; }
      for (const auto& image : latest_) {
        if (!image) { return false; }
      }
      if (latest_.size() == 2) {
        const int64_t left = rclcpp::Time(latest_[0]->header.stamp).nanoseconds();
        const int64_t right = rclcpp::Time(latest_[1]->header.stamp).nanoseconds();
        if (left - right > 30000000) { latest_[1].reset(); return false; }
        if (right - left > 30000000) { latest_[0].reset(); return false; }
      }
      return true;
    };
    if (!frame_ready_.wait_for(lock, timeout, ready) || stopped_) { return false; }
    frames = latest_;
    for (auto& image : latest_) { image.reset(); }
  }
  cv::Mat bgr = ImageToBgr(*frames[0]);
  if (frames.size() == 2) {
    cv::Mat right = ImageToBgr(*frames[1]);
    if (bgr.size() != right.size()) {
      throw std::invalid_argument("Stereo ROS images must have matching dimensions");
    }
    cv::Mat stitched;
    cv::hconcat(bgr, right, stitched);
    bgr = std::move(stitched);
  }
  cv::cvtColor(bgr, *output, cv::COLOR_BGR2YUV_I420);
  return true;
}

}