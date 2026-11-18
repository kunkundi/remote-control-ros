#pragma once

#include "rtc_camera/dual/async_image_source.h"

#include <chrono>
#include <memory>
#include <string>

namespace rtc_camera {
namespace dual {
class DualUyvyFrameConverter;
}  // 命名空间 dual
}  // 命名空间 rtc_camera

namespace rtc_runtime {
class RtcSession;
}  // 命名空间 rtc_runtime

namespace rtc_edge {

class RosImageSource;
class DirectImageSource;

struct DualCameraStreamingModuleOptions {
  rtc_camera::dual::AsyncDualCameraImageSourceOptions capture;
};

class DualCameraStreamingModule {
 public:
  explicit DualCameraStreamingModule(
      const DualCameraStreamingModuleOptions& options);
  ~DualCameraStreamingModule();

  DualCameraStreamingModule(const DualCameraStreamingModule&) = delete;
  DualCameraStreamingModule& operator=(
      const DualCameraStreamingModule&) = delete;

  bool Start(std::string* error_message);
  void RequestStop();
  void Stop();
  bool Tick(rtc_runtime::RtcSession* rtc_session,
            bool send_frame,
            bool continuous,
            std::string* error_message);

  bool started() const { return started_; }
  uint64_t captured_frames() const;

 private:
  DualCameraStreamingModuleOptions options_;
  std::unique_ptr<RosImageSource> ros_source_;
  std::unique_ptr<DirectImageSource> direct_source_;
  bool started_ = false;
  bool first_frame_logged_ = false;
};

}  // 命名空间 rtc_edge
