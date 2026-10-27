#include "audio_capture_node.h"

#include <chrono>
#include <iostream>
#include <stdexcept>
#include <thread>

namespace {

void Require(bool condition, const std::string& message) {
  if (!condition) { throw std::runtime_error(message); }
}

void CheckCapture(int sample_rate, int channels) {
  auto options = rclcpp::NodeOptions().parameter_overrides({
      rclcpp::Parameter("topic", "/audio_node_test/capture"),
      rclcpp::Parameter("sample_rate", sample_rate),
      rclcpp::Parameter("channels", channels),
      rclcpp::Parameter("frame_id", "test_microphone")});
  auto capture = std::make_shared<dozer_audio::AudioCaptureNode>(options);
  auto listener = std::make_shared<rclcpp::Node>("audio_capture_test_listener");
  dozer_msgs::msg::AudioFrame::ConstSharedPtr received;
  auto subscription = listener->create_subscription<dozer_msgs::msg::AudioFrame>(
      "/audio_node_test/capture", rclcpp::SensorDataQoS().keep_last(2),
      [&](dozer_msgs::msg::AudioFrame::ConstSharedPtr message) { received = message; });
  rclcpp::executors::SingleThreadedExecutor executor;
  executor.add_node(capture);
  executor.add_node(listener);
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
  while (!received && std::chrono::steady_clock::now() < deadline) {
    executor.spin_some();
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }
  Require(received != nullptr, "No PCM received from dummy capture node");
  Require(received->sample_rate == static_cast<uint32_t>(sample_rate) &&
              received->channels == channels && received->bits_per_sample == 16 &&
              received->frame_count == static_cast<uint32_t>(sample_rate / 100),
          "Unexpected capture format");
  Require(received->data.size() == received->frame_count * channels * 2,
          "PCM payload size does not match its metadata");
  Require(received->header.frame_id == "test_microphone" &&
              rclcpp::Time(received->header.stamp).nanoseconds() > 0,
          "Missing frame identity or timestamp");
  executor.remove_node(capture);
  capture.reset();
  Require(rclcpp::ok(), "Capture destruction shut down shared ROS context");
}

}

int main(int argc, char** argv) {
  rclcpp::init(argc, argv);
  try {
    bool rejected = false;
    try {
      auto options = rclcpp::NodeOptions().parameter_overrides({
          rclcpp::Parameter("sample_rate", 0)});
      dozer_audio::AudioCaptureNode invalid(options);
    } catch (const std::invalid_argument&) { rejected = true; }
    Require(rejected, "Invalid capture sample rate accepted");
    CheckCapture(48000, 1);
    CheckCapture(16000, 2);
    rclcpp::shutdown();
    std::cout << "Audio capture node tests passed\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    rclcpp::shutdown();
    return 1;
  }
}