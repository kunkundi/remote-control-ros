#include "dozer_rtc_edge/ros_image_source.h"
#include "rtc_edge/dual_camera_streaming_module.h"
#include "rtc_edge/single_camera_streaming_module.h"
#include "rtc_runtime/rtc_session.h"
#include <functional>
#include <future>
#include <iostream>
#include <stdexcept>

void Require(bool condition, const std::string& message) {
  if (!condition) { throw std::runtime_error(message); }
}

void Reject(const std::function<void()>& operation) {
  bool rejected = false;
  try { operation(); } catch (const std::invalid_argument&) { rejected = true; }
  Require(rejected, "Invalid image/input accepted");
}

template <typename Module>
void CheckIdleStreaming(Module& module) {
  rtc_runtime::RtcSession session(rtc_runtime::SessionOptions{});
  std::string error;
  Require(module.Start(&error), "Streaming module start failed: " + error);
  Require(module.Tick(&session, true, false, &error), "Nonblocking tick failed: " + error);
  const auto started = std::chrono::steady_clock::now();
  Require(module.Tick(&session, true, true, &error), "Idle tick failed: " + error);
  Require(std::chrono::steady_clock::now() - started >= std::chrono::milliseconds(80),
          "Continuous streaming is busy-polling empty input");
  auto tick = std::async(std::launch::async, [&] {
    return module.Tick(&session, true, true, &error);
  });
  Require(tick.wait_for(std::chrono::milliseconds(20)) == std::future_status::timeout,
          "Continuous tick did not wait");
  module.RequestStop();
  Require(module.started(), "RequestStop destroyed input before the streaming thread joined");
  Require(tick.wait_for(std::chrono::milliseconds(500)) == std::future_status::ready && tick.get(),
          "Streaming tick did not stop cleanly");
  module.Stop();
  Require(!module.started(), "Streaming module did not stop");
  Require(module.Start(&error), "Streaming module restart failed: " + error);
  module.Stop();
}

void CheckThirtyFps() {
  constexpr int frame_count = 90;
  constexpr int target_fps = 30;
  auto node = std::make_shared<rclcpp::Node>("rtc_image_rate_publisher");
  std::vector<std::string> topics;
  std::vector<rclcpp::Publisher<sensor_msgs::msg::Image>::SharedPtr> publishers;
  for (int index = 0; index < 6; ++index) {
    topics.push_back("/rtc_test/rate_" + std::to_string(index));
    publishers.push_back(node->create_publisher<sensor_msgs::msg::Image>(
      topics.back(), rclcpp::SensorDataQoS()));
  }
  std::vector<std::unique_ptr<rtc_edge::RosImageSource>> sources;
  sources.push_back(std::make_unique<rtc_edge::RosImageSource>(
    std::vector<std::string>{topics[0], topics[1]}));
  for (size_t index = 2; index < topics.size(); ++index) {
    sources.push_back(std::make_unique<rtc_edge::RosImageSource>(
      std::vector<std::string>{topics[index]}));
  }
  const auto discovery_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
  for (const auto& publisher : publishers) {
    while (publisher->get_subscription_count() != 1) {
      Require(std::chrono::steady_clock::now() < discovery_deadline,
        "30 FPS publisher discovery timed out");
      std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
  }
  sensor_msgs::msg::Image image;
  image.width = 640;
  image.height = 360;
  image.step = image.width * 3;
  image.encoding = "bgr8";
  image.data.assign(image.step * image.height, 64);
  const auto start = std::chrono::steady_clock::now() + std::chrono::milliseconds(50);
  const auto deadline = start + std::chrono::seconds(3) + std::chrono::milliseconds(300);
  std::vector<std::future<int>> consumers;
  for (size_t index = 0; index < sources.size(); ++index) {
    consumers.push_back(std::async(std::launch::async, [&, index] {
      int converted = 0;
      cv::Mat output;
      while (std::chrono::steady_clock::now() < deadline) {
        if (sources[index]->TakeI420(&output, std::chrono::milliseconds(100))) {
          Require(output.cols == (index == 0 ? 1280 : 640) && output.rows == 540,
            "30 FPS image dimensions changed");
          ++converted;
        }
      }
      return converted;
    }));
  }
  for (int frame = 0; frame < frame_count; ++frame) {
    std::this_thread::sleep_until(start + std::chrono::nanoseconds(
      static_cast<int64_t>(frame) * 1000000000 / target_fps));
    image.header.stamp = rclcpp::Time(10000000000LL +
      static_cast<int64_t>(frame) * 1000000000 / target_fps);
    for (const auto& publisher : publishers) {
      publisher->publish(image);
    }
  }
  Require(std::chrono::steady_clock::now() - start < std::chrono::milliseconds(3200),
    "Six publishers could not sustain 30 FPS");
  for (size_t index = 0; index < sources.size(); ++index) {
    const int converted = consumers[index].get();
    const uint64_t input_count = index == 0 ? 2 : 1;
    std::cout << "30 FPS input " << index << ": received=" << sources[index]->received()
              << " converted=" << converted << '/' << frame_count << '\n';
    Require(sources[index]->received() >= (frame_count - 2) * input_count,
      "ROS subscription could not sustain 30 FPS");
    Require(converted >= frame_count - 2 && converted <= frame_count,
      "Frame wait or conversion could not sustain 30 FPS");
  }
}

int main(int argc, char** argv) {
  try {
    sensor_msgs::msg::Image image;
    image.width = 4;
    image.height = 2;
    image.step = 16;
    image.encoding = "bgr8";
    image.data.assign(32, 0);
    image.data[0] = 10;
    Require(rtc_edge::ImageToBgr(image).at<cv::Vec3b>(0, 0)[0] == 10, "BGR/stride conversion failed");
    image.encoding = "rgb8";
    Require(rtc_edge::ImageToBgr(image).at<cv::Vec3b>(0, 0)[2] == 10, "RGB conversion failed");
    image.encoding = "mono8";
    Reject([&] { rtc_edge::ImageToBgr(image); });
    image.encoding = "bgr8";
    image.data.resize(1);
    Reject([&] { rtc_edge::ImageToBgr(image); });
    image.data.resize(32);
    image.width = 3;
    Reject([&] { rtc_edge::ImageToBgr(image); });
    Reject([] { rtc_edge::RosImageSource source({"/dev/video0"}); });
    image.width = 4;
    const bool standalone = argc == 2 && std::string(argv[1]) == "--standalone-context";
    if (!standalone) { rclcpp::init(argc, argv); }
        {
          rtc_edge::RosImageSource idle({"/rtc_test/idle"});
          cv::Mat output;
          const auto started = std::chrono::steady_clock::now();
          Require(!idle.TakeI420(&output, std::chrono::milliseconds(100)), "Empty source returned a frame");
          Require(std::chrono::steady_clock::now() - started >= std::chrono::milliseconds(80),
            "Empty source did not wait for a frame");
          auto stopped = std::async(std::launch::async, [&] {
      return idle.TakeI420(&output, std::chrono::seconds(5));
          });
          Require(stopped.wait_for(std::chrono::milliseconds(50)) == std::future_status::timeout,
            "Frame wait returned before stop");
          idle.RequestStop();
          Require(stopped.wait_for(std::chrono::milliseconds(500)) == std::future_status::ready,
            "Stop did not wake the frame waiter");
          Require(!stopped.get(), "Stopped input returned a frame");
          idle.RequestStop();
          Require(!idle.TakeI420(&output), "Stopped input restarted");
        }
        {
          rtc_edge::SingleCameraStreamingModuleOptions single_options;
          single_options.capture.device = "/rtc_test/stream_single";
          single_options.video_source_id = "test_single";
          rtc_edge::SingleCameraStreamingModule single(single_options);
          CheckIdleStreaming(single);
          rtc_edge::DualCameraStreamingModuleOptions stereo_options;
          stereo_options.left_device = "/rtc_test/stream_left";
          stereo_options.right_device = "/rtc_test/stream_right";
          rtc_edge::DualCameraStreamingModule stereo(stereo_options);
          CheckIdleStreaming(stereo);
        }
    {
      rtc_edge::RosImageSource single({"/rtc_test/left"});
      rtc_edge::RosImageSource stereo({"/rtc_test/left", "/rtc_test/right"});
      auto node = std::make_shared<rclcpp::Node>("rtc_image_test_publisher");
      auto left = node->create_publisher<sensor_msgs::msg::Image>("/rtc_test/left", rclcpp::SensorDataQoS());
      auto right = node->create_publisher<sensor_msgs::msg::Image>("/rtc_test/right", rclcpp::SensorDataQoS());
      auto wait_until = [](const std::string& stage, const std::function<bool()>& check) {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (!check()) {
          Require(std::chrono::steady_clock::now() < deadline, "ROS image test timed out: " + stage);
          std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
      };
      wait_until("discovery", [&] { return left->get_subscription_count() == 2 && right->get_subscription_count() == 1; });
      {
        rtc_edge::RosImageSource waiting({"/rtc_test/wake"});
        auto publisher = node->create_publisher<sensor_msgs::msg::Image>("/rtc_test/wake", rclcpp::SensorDataQoS());
        wait_until("waiting input discovery", [&] { return publisher->get_subscription_count() == 1; });
        cv::Mat output;
        auto received = std::async(std::launch::async, [&] {
          return waiting.TakeI420(&output, std::chrono::seconds(5));
        });
        Require(received.wait_for(std::chrono::milliseconds(50)) == std::future_status::timeout,
                "Frame wait returned without an image");
        wait_until("frame arrival wakes input", [&] {
          publisher->publish(image);
          return received.wait_for(std::chrono::milliseconds(5)) == std::future_status::ready;
        });
        Require(received.get() && output.cols == 4 && output.rows == 3, "Frame arrival did not wake conversion");
      }
      image.header.stamp.sec = 10;
      left->publish(image);
      image.header.stamp.sec = 11;
      right->publish(image);
      wait_until("initial frames", [&] {
        image.header.stamp.sec = 10;
        left->publish(image);
        image.header.stamp.sec = 11;
        right->publish(image);
        return stereo.received() >= 4 && single.received() >= 2;
      });
      cv::Mat output;
      Require(single.TakeI420(&output) && output.cols == 4 && output.rows == 3, "Single I420 conversion failed");
      uint64_t drained_count = 0;
      wait_until("drain pending frames", [&] {
        drained_count = single.received();
        return !single.TakeI420(&output);
      });
      const bool another_frame = single.TakeI420(&output);
      Require(!another_frame || single.received() > drained_count, "Consumed frame was reused");
      Require(!stereo.TakeI420(&output), "Unsynchronized stereo frames accepted");
      wait_until("paired frame", [&] {
        left->publish(image);
        right->publish(image);
        return stereo.TakeI420(&output);
      });
      Require(output.cols == 8 && output.rows == 3, "Stereo pairing failed");
      {
        rtc_edge::RosImageSource temporary({"/rtc_test/right"});
        wait_until("temporary input", [&] {
          right->publish(image);
          return temporary.received() > 0;
        });
      }
      const auto single_before = single.received();
      const auto stereo_before = stereo.received();
      wait_until("remaining inputs after peer stopped", [&] {
        left->publish(image);
        right->publish(image);
        return single.received() > single_before && stereo.received() > stereo_before;
      });
    }
    CheckThirtyFps();
    Require(rclcpp::ok(), "Destroying camera inputs shut down shared ROS context");
    if (!standalone) { rclcpp::shutdown(); }
    std::cout << "ROS image input tests passed\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    if (rclcpp::ok()) { rclcpp::shutdown(); }
    return 1;
  }
}