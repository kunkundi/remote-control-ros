#include "dozer_rtc_edge/ros_image_source.h"
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
    ros_source_ = std::make_unique<RosImageSource>(std::vector<std::string>{options_.capture.device});
    started_ = true;
    return true;
  } catch (const std::exception& error) {
    if (error_message) { *error_message = error.what(); }
    return false;
  }
}
void SingleCameraStreamingModule::RequestStop() { Stop(); }
void SingleCameraStreamingModule::Stop() { ros_source_.reset(); started_ = false; }
bool SingleCameraStreamingModule::started() const { return started_; }
uint64_t SingleCameraStreamingModule::captured_frames() const {
  return ros_source_ ? ros_source_->received() : 0;
}
bool SingleCameraStreamingModule::Tick(rtc_runtime::RtcSession* session,
    bool send_frame, bool, std::string* error_message) {
  if (!started_ || !session) {
    if (error_message) { *error_message = "ROS camera stream is not started"; }
    return false;
  }
  if (!send_frame) { return true; }
  try {
    cv::Mat i420;
    if (!ros_source_->TakeI420(&i420)) { return true; }
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
    if (options_.yolo_enabled) {
      throw std::invalid_argument("ROS camera input uses dozer_perception; disable edge local YOLO in rtc.cfg");
    }
    ros_source_ = std::make_unique<RosImageSource>(std::vector<std::string>{
        options_.capture.left_device, options_.capture.right_device});
    started_ = true;
    return true;
  } catch (const std::exception& error) {
    if (error_message) { *error_message = error.what(); }
    return false;
  }
}
void DualCameraStreamingModule::RequestStop() { Stop(); }
void DualCameraStreamingModule::Stop() { ros_source_.reset(); started_ = false; }
uint64_t DualCameraStreamingModule::captured_frames() const {
  return ros_source_ ? ros_source_->received() / 2 : 0;
}
bool DualCameraStreamingModule::Tick(rtc_runtime::RtcSession* session,
    bool send_frame, bool, std::string* error_message) {
  if (!started_ || !session) {
    if (error_message) { *error_message = "ROS stereo stream is not started"; }
    return false;
  }
  if (!send_frame) { return true; }
  try {
    cv::Mat i420;
    if (!ros_source_->TakeI420(&i420)) { return true; }
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