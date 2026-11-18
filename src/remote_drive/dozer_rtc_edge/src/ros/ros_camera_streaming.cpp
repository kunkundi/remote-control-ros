#include "dozer_rtc_edge/ros_image_source.h"
#include "dozer_rtc_edge/direct_image_source.h"
#include "rtc_edge/single_camera_streaming_module.h"
#include "rtc_edge/dual_camera_streaming_module.h"
#include "rtc_edge/camera_video_sources.h"
#include "rtc_camera/single/frame_converter.h"
#include "rtc_runtime/rtc_session.h"

namespace rtc_edge {

SingleCameraStreamingModule::SingleCameraStreamingModule(
    const SingleCameraStreamingModuleOptions& options) : options_(options) {}
SingleCameraStreamingModule::~SingleCameraStreamingModule() { Stop(); }
bool SingleCameraStreamingModule::Start(std::string* error_message) {
  if (started_) { return true; }
  try {
    if (options_.video_source_id.empty()) {
      throw std::invalid_argument("RTC video source ID must not be empty");
    }
    if (options_.capture.device.find("/dev/video") == 0) {
      std::string camera_name = options_.video_source_id;
      const std::string surround_prefix = "surround_";
      if (camera_name.find(surround_prefix) == 0) {
        camera_name.erase(0, surround_prefix.size());
      }
      direct_source_ = std::make_unique<DirectImageSource>(
          std::vector<std::string>{options_.capture.device},
          std::vector<std::string>{camera_name});
    } else {
      ros_source_ = std::make_unique<RosImageSource>(std::vector<std::string>{options_.capture.device});
    }
    started_ = true;
    return true;
  } catch (const std::exception& error) {
    if (error_message) { *error_message = error.what(); }
    return false;
  }
}
void SingleCameraStreamingModule::RequestStop() {
  if (direct_source_) { direct_source_->RequestStop(); }
  if (ros_source_) { ros_source_->RequestStop(); }
}
void SingleCameraStreamingModule::Stop() { direct_source_.reset(); ros_source_.reset(); started_ = false; }
bool SingleCameraStreamingModule::started() const { return started_; }
uint64_t SingleCameraStreamingModule::captured_frames() const {
  if (direct_source_) return direct_source_->received();
  return ros_source_ ? ros_source_->received() : 0;
}
bool SingleCameraStreamingModule::Tick(rtc_runtime::RtcSession* session,
  bool send_frame, bool continuous, std::string* error_message) {
  if (!started_ || !session) {
    if (error_message) { *error_message = "ROS camera stream is not started"; }
    return false;
  }
  if (!send_frame) { return true; }
  try {
    cv::Mat i420;
    const auto timeout = std::chrono::milliseconds(continuous ? 100 : 0);
    if (!(direct_source_ ? direct_source_->TakeI420(&i420, timeout) :
                ros_source_->TakeI420(&i420, timeout))) { return true; }
    session->NoteCapturedFrame();
    if (!session->IsReadyToSend()) { return true; }
    rtc_camera::single::ConvertedCameraFrame frame;
    frame.data = i420.data;
    frame.data_size = i420.total();
    frame.width = i420.cols;
    frame.height = i420.rows * 2 / 3;
    frame.stride_y = frame.width;
    frame.stride_u = frame.stride_v = frame.width / 2;
    if (options_.frame_handler) { return options_.frame_handler(session, frame); }
    if (session->SendI420Frame(options_.video_source_id.c_str(), frame.data,
        frame.data_size, frame.width, frame.height, frame.stride_y,
        frame.stride_u, frame.stride_v)) { return true; }
    if (error_message) { *error_message = "RTC ROS image send failed"; }
    return false;
  } catch (const std::exception& error) {
    if (error_message) { *error_message = error.what(); }
    return false;
  }
}

DualCameraStreamingModule::DualCameraStreamingModule(
    const DualCameraStreamingModuleOptions& options) : options_(options) {}
DualCameraStreamingModule::~DualCameraStreamingModule() { Stop(); }
bool DualCameraStreamingModule::Start(std::string* error_message) {
  if (started_) { return true; }
  try {
    const bool direct_left = options_.capture.left_device.find("/dev/video") == 0;
    const bool direct_right = options_.capture.right_device.find("/dev/video") == 0;
    if (direct_left != direct_right) {
      throw std::invalid_argument("Stereo inputs must use the same capture backend");
    }
    if (direct_left) {
      direct_source_ = std::make_unique<DirectImageSource>(
          std::vector<std::string>{options_.capture.left_device, options_.capture.right_device},
          std::vector<std::string>{"front_left", "front_right"});
    } else {
      ros_source_ = std::make_unique<RosImageSource>(std::vector<std::string>{
          options_.capture.left_device, options_.capture.right_device});
    }
    started_ = true;
    return true;
  } catch (const std::exception& error) {
    if (error_message) { *error_message = error.what(); }
    return false;
  }
}
void DualCameraStreamingModule::RequestStop() {
  if (direct_source_) { direct_source_->RequestStop(); }
  if (ros_source_) { ros_source_->RequestStop(); }
}
void DualCameraStreamingModule::Stop() { direct_source_.reset(); ros_source_.reset(); started_ = false; }
uint64_t DualCameraStreamingModule::captured_frames() const {
  if (direct_source_) return direct_source_->received() / 2;
  return ros_source_ ? ros_source_->received() / 2 : 0;
}
bool DualCameraStreamingModule::Tick(rtc_runtime::RtcSession* session,
  bool send_frame, bool continuous, std::string* error_message) {
  if (!started_ || !session) {
    if (error_message) { *error_message = "ROS stereo stream is not started"; }
    return false;
  }
  if (!send_frame) { return true; }
  try {
    cv::Mat i420;
    const auto timeout = std::chrono::milliseconds(continuous ? 100 : 0);
    if (!(direct_source_ ? direct_source_->TakeI420(&i420, timeout) :
                ros_source_->TakeI420(&i420, timeout))) { return true; }
    session->NoteCapturedFrame();
    if (!session->IsReadyToSend()) { return true; }
    if (session->SendI420Frame(kStereoCameraVideoSourceId, i420.data,
        i420.total(), i420.cols, i420.rows * 2 / 3,
        i420.cols, i420.cols / 2, i420.cols / 2)) { return true; }
    if (error_message) { *error_message = "RTC ROS stereo send failed"; }
    return false;
  } catch (const std::exception& error) {
    if (error_message) { *error_message = error.what(); }
    return false;
  }
}

}