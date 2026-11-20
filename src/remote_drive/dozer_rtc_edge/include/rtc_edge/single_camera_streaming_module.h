#pragma once

#include "rtc_camera/camera_capture_options.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace rtc_camera {
namespace single {
struct ConvertedCameraFrame;
}  // 命名空间 single
}  // 命名空间 rtc_camera

namespace rtc_runtime {
class RtcSession;
}

namespace rtc_edge {

class RosImageSource;
class DirectImageSource;

struct SingleCameraStreamingModuleOptions {
  rtc_camera::CameraCaptureOptions capture;
  std::string video_source_id;
  std::function<bool(rtc_runtime::RtcSession*, const rtc_camera::single::ConvertedCameraFrame&)> frame_handler;
};

class SingleCameraStreamingModule {
 public:
  explicit SingleCameraStreamingModule(
      const SingleCameraStreamingModuleOptions& options);
  ~SingleCameraStreamingModule();

  SingleCameraStreamingModule(const SingleCameraStreamingModule&) = delete;
  SingleCameraStreamingModule& operator=(
      const SingleCameraStreamingModule&) = delete;

  bool Start(std::string* error_message);
  void RequestStop();
  void Stop();
  bool Tick(rtc_runtime::RtcSession* rtc_session,
            bool send_frame,
            bool continuous,
            std::string* error_message);

  bool started() const;
  uint64_t captured_frames() const;

 private:
  SingleCameraStreamingModuleOptions options_;
  std::unique_ptr<RosImageSource> ros_source_;
  std::unique_ptr<DirectImageSource> direct_source_;
  bool started_ = false;
  bool first_frame_logged_ = false;
};

}  // 命名空间 rtc_edge
