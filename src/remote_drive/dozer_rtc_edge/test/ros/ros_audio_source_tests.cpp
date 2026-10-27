#include "dozer_rtc_edge/ros_audio_source.h"
#include "dozer_rtc_edge/ros_image_source.h"

#include <atomic>
#include <chrono>
#include <functional>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {

void Require(bool condition, const std::string& message) {
  if (!condition) { throw std::runtime_error(message); }
}

void WaitUntil(const std::string& stage, const std::function<bool()>& check) {
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
  while (!check()) {
    Require(std::chrono::steady_clock::now() < deadline, "Audio test timed out: " + stage);
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
}

void Reject(const std::function<void()>& operation) {
  bool rejected = false;
  try { operation(); } catch (const std::invalid_argument&) { rejected = true; }
  Require(rejected, "Invalid PCM frame or input accepted");
}

void TestValidation() {
  for (uint32_t rate : {8000, 16000, 32000, 44100, 48000}) {
    for (uint16_t channels : {1, 2}) {
      dozer_msgs::msg::AudioFrame message;
      message.sample_rate = rate;
      message.channels = channels;
      message.bits_per_sample = 16;
      message.frame_count = rate / 100;
      message.data.assign(message.frame_count * channels * 2, 7);
      const auto frame = rtc_edge::AudioFrameToView(message);
      Require(frame.sample_rate == rate && frame.number_of_channels == channels &&
                  frame.bits_per_sample == 16 && frame.number_of_frames == rate / 100 &&
                  frame.data == message.data.data() && frame.data_size == message.data.size(),
              "PCM metadata or buffer was changed");
      auto invalid = message;
      invalid.data.pop_back();
      Reject([&] { rtc_edge::AudioFrameToView(invalid); });
      invalid = message;
      invalid.bits_per_sample = 8;
      Reject([&] { rtc_edge::AudioFrameToView(invalid); });
      invalid = message;
      invalid.channels = 3;
      Reject([&] { rtc_edge::AudioFrameToView(invalid); });
      invalid = message;
      invalid.sample_rate = 12000;
      Reject([&] { rtc_edge::AudioFrameToView(invalid); });
      invalid = message;
      invalid.frame_count = std::numeric_limits<uint32_t>::max();
      Reject([&] { rtc_edge::AudioFrameToView(invalid); });
      invalid = message;
      invalid.frame_count *= 2;
      invalid.data.resize(invalid.data.size() * 2);
      Reject([&] { rtc_edge::AudioFrameToView(invalid); });
    }
  }
  Reject([] { rtc_edge::AudioFrameToView(dozer_msgs::msg::AudioFrame()); });
  Reject([] { rtc_edge::RosAudioSource source("default", [](const auto&) {}); });
  Reject([] { rtc_edge::RosAudioSource source("/dev/snd/mic", [](const auto&) {}); });
  Reject([] { rtc_edge::RosAudioSource source("/audio/test", {}); });
}

void TestSubscription() {
  std::atomic<uint64_t> received{0};
  std::atomic<bool> invalid_forwarded{false};
  rtc_edge::RosAudioSource input("/rtc_audio_test/capture", [&](const auto& frame) {
    if (frame.sample_rate != 48000 || frame.number_of_frames != 480 ||
        frame.number_of_channels != 1 || frame.data_size != 960 ||
        static_cast<const uint8_t*>(frame.data)[0] != 37) {
      invalid_forwarded.store(true);
    }
    ++received;
  });
  rtc_edge::RosImageSource camera({"/rtc_audio_test/image"});
  auto node = std::make_shared<rclcpp::Node>("rtc_audio_test_publisher");
  auto publisher = node->create_publisher<dozer_msgs::msg::AudioFrame>(
      "/rtc_audio_test/capture", rclcpp::SensorDataQoS().keep_last(2));
  WaitUntil("discovery", [&] { return publisher->get_subscription_count() == 1; });
  dozer_msgs::msg::AudioFrame message;
  message.sample_rate = 48000;
  message.channels = 1;
  message.bits_per_sample = 16;
  message.frame_count = 480;
  message.data.assign(960, 37);
  auto invalid = message;
  invalid.data.clear();
  WaitUntil("invalid followed by valid PCM", [&] {
    publisher->publish(invalid);
    publisher->publish(message);
    return received.load() > 0;
  });
  {
    std::atomic<bool> temporary_received{false};
    rtc_edge::RosAudioSource temporary("/rtc_audio_test/capture", [&](const auto&) {
      temporary_received.store(true);
    });
    WaitUntil("temporary subscription", [&] {
      publisher->publish(message);
      return temporary_received.load();
    });
  }
  const auto before = received.load();
  WaitUntil("remaining input after peer stopped", [&] {
    publisher->publish(message);
    return received.load() > before;
  });
  Require(!invalid_forwarded.load(), "Invalid PCM was forwarded or valid PCM changed");
}

void TestLiveCapture() {
  std::atomic<bool> received{false};
  rtc_edge::RosAudioSource input("/rtc_audio_test/live", [&](const auto& frame) {
    if (frame.sample_rate == 48000 && frame.number_of_channels == 1 &&
        frame.bits_per_sample == 16 && frame.number_of_frames == 480 && frame.data_size == 960) {
      received.store(true);
    }
  });
  WaitUntil("dummy capture node to edge subscriber", [&] { return received.load(); });
}

}

int main(int argc, char** argv) {
  try {
    TestValidation();
    const bool standalone = argc == 2 && std::string(argv[1]) == "--standalone-context";
    const bool live = argc == 2 && std::string(argv[1]) == "--live-capture";
    if (!standalone) { rclcpp::init(argc, argv); }
    if (live) { TestLiveCapture(); } else { TestSubscription(); }
    Require(rclcpp::ok(), "Destroying audio inputs shut down shared ROS context");
    if (!standalone) { rclcpp::shutdown(); }
    std::cout << "ROS audio input tests passed\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    if (rclcpp::ok()) { rclcpp::shutdown(); }
    return 1;
  }
}