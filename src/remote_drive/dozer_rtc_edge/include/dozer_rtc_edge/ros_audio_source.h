#pragma once

#include "rtc_audio/audio_device.h"

#include <dozer_msgs/msg/audio_frame.hpp>
#include <rclcpp/rclcpp.hpp>
#include <memory>
#include <string>
#include <thread>

namespace rtc_edge {

rtc_audio::AudioFrameView AudioFrameToView(const dozer_msgs::msg::AudioFrame& message);

class RosAudioSource {
 public:
  RosAudioSource(const std::string& topic, const rtc_audio::AudioFrameCallback& callback);
  ~RosAudioSource();

 private:
  std::shared_ptr<rclcpp::Context> context_;
  rclcpp::Node::SharedPtr node_;
  std::unique_ptr<rclcpp::executors::SingleThreadedExecutor> executor_;
  rclcpp::Subscription<dozer_msgs::msg::AudioFrame>::SharedPtr subscription_;
  std::thread worker_;
};

}