#include "dozer_rtc_edge/ros_audio_source.h"
#include "dozer_rtc_edge/ros_context.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <stdexcept>

namespace rtc_edge {

rtc_audio::AudioFrameView AudioFrameToView(const dozer_msgs::msg::AudioFrame& message) {
  const std::array<uint32_t, 5> supported_rates{{8000, 16000, 32000, 44100, 48000}};
  if (message.bits_per_sample != 16 || message.channels < 1 || message.channels > 2 ||
      std::find(supported_rates.begin(), supported_rates.end(), message.sample_rate) ==
          supported_rates.end() || message.frame_count != message.sample_rate / 100 ||
      message.data.size() != static_cast<size_t>(message.frame_count) * message.channels * 2) {
    throw std::invalid_argument("Expected a complete 10 ms signed 16-bit PCM frame");
  }
  rtc_audio::AudioFrameView frame;
  frame.bits_per_sample = message.bits_per_sample;
  frame.sample_rate = message.sample_rate;
  frame.number_of_channels = message.channels;
  frame.number_of_frames = message.frame_count;
  frame.data = message.data.data();
  frame.data_size = message.data.size();
  return frame;
}

RosAudioSource::RosAudioSource(const std::string& topic,
                               const rtc_audio::AudioFrameCallback& callback) {
  if (topic.empty() || topic.front() != '/' || topic.find("/dev/") == 0 || !callback) {
    throw std::invalid_argument("Audio input requires an absolute ROS topic and a callback");
  }
  context_ = AcquireRosContext();
  rclcpp::NodeOptions options;
  options.context(context_);
  static std::atomic<uint64_t> instance{0};
  node_ = std::make_shared<rclcpp::Node>("rtc_audio_input_" + std::to_string(instance++), options);
  rclcpp::ExecutorOptions executor_options;
  executor_options.context = context_;
  executor_ = std::make_unique<rclcpp::executors::SingleThreadedExecutor>(executor_options);
  subscription_ = node_->create_subscription<dozer_msgs::msg::AudioFrame>(
      topic, rclcpp::SensorDataQoS().keep_last(2),
      [this, callback](dozer_msgs::msg::AudioFrame::ConstSharedPtr message) {
        rtc_audio::AudioFrameView frame;
        try {
          frame = AudioFrameToView(*message);
        } catch (const std::invalid_argument& error) {
          RCLCPP_WARN_THROTTLE(node_->get_logger(), *node_->get_clock(), 2000,
                               "Dropping invalid audio: %s", error.what());
          return;
        }
        callback(frame);
      });
  executor_->add_node(node_);
  worker_ = std::thread([this] { executor_->spin(); });
}

RosAudioSource::~RosAudioSource() {
  executor_->cancel();
  if (worker_.joinable()) { worker_.join(); }
  executor_->remove_node(node_);
}

}