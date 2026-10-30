#include "dozer_camera_gmsl/frame_rate_limiter.hpp"

#include <iostream>
#include <limits>
#include <stdexcept>

using dozer_camera_gmsl::FrameRateLimiter;

void Require(bool condition, const char * message)
{
  if (!condition) {
    throw std::runtime_error(message);
  }
}

FrameRateLimiter::TimePoint At(double seconds)
{
  return FrameRateLimiter::TimePoint(FrameRateLimiter::Duration(seconds));
}

int main()
{
  try {
    FrameRateLimiter jittered(30.0);
    for (int frame = 0; frame < 300; ++frame) {
      const double jitter = frame % 2 == 0 ? 0.004 : -0.004;
      Require(jittered.ShouldPublish(At(1.0 + frame / 30.0 + jitter)),
        "30 FPS input lost a frame because of scheduling jitter");
    }
    for (const double input_fps : {30.0, 60.0, 120.0}) {
      FrameRateLimiter limiter(30.0);
      int published = 0;
      for (int frame = 0; frame < static_cast<int>(input_fps * 10); ++frame) {
        published += limiter.ShouldPublish(At(frame / input_fps)) ? 1 : 0;
      }
      Require(published >= 299 && published <= 301, "Output did not stay at 30 FPS");
    }
    FrameRateLimiter slow(30.0);
    for (int frame = 0; frame < 150; ++frame) {
      Require(slow.ShouldPublish(At(frame / 15.0)), "Slow camera frames were discarded");
    }
    FrameRateLimiter resumed(30.0);
    Require(resumed.ShouldPublish(At(0.0)), "First frame was not published");
    Require(!resumed.ShouldPublish(At(0.0)), "Duplicate timestamp was published");
    Require(resumed.ShouldPublish(At(10.0)), "Capture did not recover after a stall");
    Require(!resumed.ShouldPublish(At(10.001)), "Stall caused a catch-up burst");
    Require(resumed.ShouldPublish(At(1.0)), "Clock reset did not recover");
    for (const double invalid : {0.0, -1.0, std::numeric_limits<double>::infinity(),
        std::numeric_limits<double>::quiet_NaN()})
    {
      bool rejected = false;
      try {
        FrameRateLimiter limiter(invalid);
      } catch (const std::invalid_argument &) {
        rejected = true;
      }
      Require(rejected, "Invalid frame rate was accepted");
    }
    std::cout << "Frame rate limiter tests passed\n";
    return 0;
  } catch (const std::exception & error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}