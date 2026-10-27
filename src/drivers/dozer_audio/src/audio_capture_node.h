#pragma once

#include "rtc_audio/audio_device.h"

#include <dozer_msgs/msg/audio_frame.hpp>
#include <rclcpp/rclcpp.hpp>
#include <string>

namespace dozer_audio {

class AudioCaptureNode : public rclcpp::Node {
 public:
  explicit AudioCaptureNode(const rclcpp::NodeOptions& options = rclcpp::NodeOptions());
  ~AudioCaptureNode() override;

 private:
  std::string frame_id_;
  rclcpp::Publisher<dozer_msgs::msg::AudioFrame>::SharedPtr publisher_;
  rtc_audio::AudioCapture capture_;
};

}