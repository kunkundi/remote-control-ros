#include "audio_capture_node.h"

#include <algorithm>
#include <array>
#include <stdexcept>

namespace dozer_audio {

AudioCaptureNode::AudioCaptureNode(const rclcpp::NodeOptions& options)
    : Node("audio_capture", options) {
  rtc_audio::AudioCaptureOptions capture_options;
  capture_options.device_name = declare_parameter<std::string>("device_name", "default");
  const auto sample_rate = declare_parameter<int64_t>("sample_rate", 48000);
  const auto channels = declare_parameter<int64_t>("channels", 1);
  const std::array<int64_t, 5> supported_rates{{8000, 16000, 32000, 44100, 48000}};
  if (std::find(supported_rates.begin(), supported_rates.end(), sample_rate) ==
          supported_rates.end() || channels < 1 || channels > 2) {
    throw std::invalid_argument("Audio requires a supported sample rate and one or two channels");
  }
  capture_options.sample_rate = static_cast<int>(sample_rate);
  capture_options.channels = static_cast<int>(channels);
  capture_options.frame_duration_ms = 10;
  frame_id_ = declare_parameter<std::string>("frame_id", "microphone");
  const auto topic = declare_parameter<std::string>("topic", "/audio/capture");
  publisher_ = create_publisher<dozer_msgs::msg::AudioFrame>(
      topic, rclcpp::SensorDataQoS().keep_last(2));
  std::string error;
  if (!capture_.Start(capture_options, [this](const rtc_audio::AudioFrameView& frame) {
        if (!rclcpp::ok(get_node_base_interface()->get_context())) { return; }
        dozer_msgs::msg::AudioFrame message;
        message.header.stamp = now();
        message.header.frame_id = frame_id_;
        message.sample_rate = static_cast<uint32_t>(frame.sample_rate);
        message.channels = static_cast<uint16_t>(frame.number_of_channels);
        message.bits_per_sample = static_cast<uint16_t>(frame.bits_per_sample);
        message.frame_count = static_cast<uint32_t>(frame.number_of_frames);
        const auto* bytes = static_cast<const uint8_t*>(frame.data);
        message.data.assign(bytes, bytes + frame.data_size);
        publisher_->publish(message);
      }, &error)) {
    throw std::runtime_error(error);
  }
  RCLCPP_INFO(get_logger(), "Publishing %ld Hz / %ld channel PCM on %s from %s",
              sample_rate, channels, publisher_->get_topic_name(),
              capture_options.device_name.c_str());
}

AudioCaptureNode::~AudioCaptureNode() { capture_.Stop(); }

}