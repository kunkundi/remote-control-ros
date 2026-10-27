#include "audio_capture_node.h"

#include <exception>

int main(int argc, char** argv) {
  rclcpp::init(argc, argv);
  int result = 0;
  try {
    rclcpp::spin(std::make_shared<dozer_audio::AudioCaptureNode>());
  } catch (const std::exception& error) {
    RCLCPP_ERROR(rclcpp::get_logger("audio_capture"), "%s", error.what());
    result = 1;
  }
  rclcpp::shutdown();
  return result;
}