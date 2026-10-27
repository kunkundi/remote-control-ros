#pragma once

#include <rclcpp/rclcpp.hpp>

namespace rtc_edge {

std::shared_ptr<rclcpp::Context> AcquireRosContext();

}