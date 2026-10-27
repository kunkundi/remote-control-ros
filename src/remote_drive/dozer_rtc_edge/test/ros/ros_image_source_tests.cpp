#include "dozer_rtc_edge/ros_image_source.h"
#include <functional>
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