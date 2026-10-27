#include "dozer_rtc_edge/ros_image_source.h"

#include <limits>
#include <stdexcept>
#include <opencv2/imgproc.hpp>

namespace rtc_edge {
namespace {

class CameraRosRuntime {
 public:
  std::shared_ptr<rclcpp::Context> Acquire() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!context_->is_valid()) {
      context_->init(0, nullptr);
      owns_initialization_ = true;
    }
    return context_;
  }

  ~CameraRosRuntime() {
    if (owns_initialization_ && context_->is_valid()) {
      context_->shutdown("camera runtime stopped");
    }
  }

 private:
  std::shared_ptr<rclcpp::Context> context_ = rclcpp::contexts::get_global_default_context();
  std::mutex mutex_;
  bool owns_initialization_ = false;
};

std::shared_ptr<rclcpp::Context> AcquireCameraContext() {
  static CameraRosRuntime runtime;
  return runtime.Acquire();
}

}

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
  context_ = AcquireCameraContext();
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
          std::lock_guard<std::mutex> lock(mutex_);
          latest_[index] = std::move(image);
          ++received_;
        }));
  }
  executor_->add_node(node_);
  worker_ = std::thread([this] { executor_->spin(); });
}

RosImageSource::~RosImageSource() {
  executor_->cancel();
  if (worker_.joinable()) { worker_.join(); }
  executor_->remove_node(node_);
}

bool RosImageSource::TakeI420(cv::Mat* output) {
  std::vector<sensor_msgs::msg::Image::ConstSharedPtr> frames;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& image : latest_) {
      if (!image) { return false; }
    }
    if (latest_.size() == 2) {
      const int64_t left = rclcpp::Time(latest_[0]->header.stamp).nanoseconds();
      const int64_t right = rclcpp::Time(latest_[1]->header.stamp).nanoseconds();
      if (left - right > 30000000) { latest_[1].reset(); return false; }
      if (right - left > 30000000) { latest_[0].reset(); return false; }
    }
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