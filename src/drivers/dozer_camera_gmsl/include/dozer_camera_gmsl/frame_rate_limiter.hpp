#pragma once

#include <chrono>
#include <cmath>
#include <stdexcept>

namespace dozer_camera_gmsl {

class FrameRateLimiter
{
public:
  using Duration = std::chrono::duration<double>;
  using TimePoint = std::chrono::time_point<std::chrono::steady_clock, Duration>;

  explicit FrameRateLimiter(double max_fps)
  {
    if (!std::isfinite(max_fps) || max_fps <= 0 || !std::isfinite(1.0 / max_fps)) {
      throw std::invalid_argument("Frame rate must be finite and positive");
    }
    period_ = Duration(1.0 / max_fps);
  }

  bool ShouldPublish(TimePoint timestamp)
  {
    if (!initialized_ || timestamp < last_frame_) {
      next_publish_ = timestamp;
      initialized_ = true;
    }
    last_frame_ = timestamp;
    if (timestamp + period_ / 2 < next_publish_) {
      return false;
    }
    next_publish_ += period_;
    if (next_publish_ <= timestamp) {
      next_publish_ = timestamp + period_;
    }
    return true;
  }

private:
  Duration period_{};
  TimePoint next_publish_{};
  TimePoint last_frame_{};
  bool initialized_ = false;
};

}