#include "dozer_rtc_edge/direct_image_source.h"
#include "dozer_rtc_edge/shared_camera_frame.h"
#include "rtc_camera/single/async_image_source.h"
#include "rtc_camera/single/frame_converter.h"
#include <libyuv.h>
#include <cstring>
#include <stdexcept>

namespace rtc_edge {
namespace {
constexpr int kWidth = 640;
constexpr int kHeight = 360;

int64_t SteadyMicros() {
  return std::chrono::duration_cast<std::chrono::microseconds>(
      std::chrono::steady_clock::now().time_since_epoch()).count();
}
}

cv::Mat StitchI420(const cv::Mat& left, const cv::Mat& right) {
  if (left.empty() || left.type() != CV_8UC1 || right.type() != CV_8UC1 ||
      left.size() != right.size() || left.rows % 3 || left.cols % 2 ||
      !left.isContinuous() || !right.isContinuous()) {
    throw std::invalid_argument("Invalid stereo I420 planes");
  }
  const int height = left.rows * 2 / 3;
  cv::Mat output(left.rows, left.cols * 2, CV_8UC1);
  size_t offset = 0;
  size_t output_offset = 0;
  for (int plane = 0; plane < 3; ++plane) {
    const size_t width = static_cast<size_t>(plane == 0 ? left.cols : left.cols / 2);
    const int rows = plane == 0 ? height : height / 2;
    for (int row = 0; row < rows; ++row) {
      std::memcpy(output.data + output_offset, left.data + offset, width);
      std::memcpy(output.data + output_offset + width, right.data + offset, width);
      offset += width;
      output_offset += width * 2;
    }
  }
  return output;
}

struct DirectImageSource::Input {
  std::unique_ptr<rtc_camera::single::AsyncCameraImageSource> capture;
  std::shared_ptr<rtc_camera::single::CameraFrameSubscription> subscription;
  std::unique_ptr<SharedCameraWriter> writer;
  std::string name;
  cv::Mat latest;
  int64_t timestamp_us = 0;
};

DirectImageSource::DirectImageSource(const std::vector<std::string>& devices,
                                     const std::vector<std::string>& camera_names) {
  if (devices.empty() || devices.size() > 2 || devices.size() != camera_names.size()) {
    throw std::invalid_argument("Expected one or two direct cameras");
  }
  try {
    for (size_t index = 0; index < devices.size(); ++index) {
      if (devices[index].find("/dev/video") != 0) {
        throw std::invalid_argument("Direct camera requires a /dev/video device");
      }
      auto input = std::make_unique<Input>();
      input->name = camera_names[index];
      rtc_camera::CameraCaptureOptions options;
      options.device = devices[index];
      options.width = 1920;
      options.height = 1080;
      options.timeout_ms = 100;
      input->capture = std::make_unique<rtc_camera::single::AsyncCameraImageSource>(options);
      input->subscription = input->capture->Subscribe(1);
        input->writer = std::make_unique<SharedCameraWriter>(
          "dozer_camera_" + input->name, kWidth * kHeight * 3 / 2);
      inputs_.push_back(std::move(input));
    }
    for (size_t index = 0; index < inputs_.size(); ++index) {
      inputs_[index]->capture->Start();
      workers_.emplace_back(&DirectImageSource::Run, this, index);
    }
    std::unique_lock<std::mutex> lock(mutex_);
    const bool started = ready_.wait_for(lock, std::chrono::seconds(5), [this] {
      if (!error_.empty()) return true;
      for (const auto& input : inputs_) if (input->latest.empty()) return false;
      return true;
    });
    if (!error_.empty()) throw std::runtime_error(error_);
    if (!started) throw std::runtime_error("Timed out waiting for direct cameras");
  } catch (...) {
    RequestStop();
    for (auto& worker : workers_) if (worker.joinable()) worker.join();
    throw;
  }
}

DirectImageSource::~DirectImageSource() {
  RequestStop();
  for (auto& worker : workers_) if (worker.joinable()) worker.join();
}

void DirectImageSource::RequestStop() {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    stopped_ = true;
  }
  for (auto& input : inputs_) {
    input->capture->RequestStop();
    input->subscription->Close();
  }
  ready_.notify_all();
}

void DirectImageSource::Run(size_t index) {
  try {
    auto& input = *inputs_[index];
    rtc_camera::single::CameraFrameConverter converter;
    while (!stopped_) {
      rtc_camera::single::CameraFrame raw;
      if (!input.subscription->WaitNext(&raw, std::chrono::milliseconds(100))) {
        if (input.capture->failed()) throw std::runtime_error(input.capture->error_message());
        continue;
      }
      rtc_camera::single::ConvertedCameraFrame converted;
      std::string error;
      if (!converter.ConvertToI420(raw, &converted, &error)) throw std::runtime_error(error);
      cv::Mat small(kHeight * 3 / 2, kWidth, CV_8UC1);
      if (libyuv::I420Scale(converted.data, converted.stride_y,
              converted.data + converted.stride_y * converted.height, converted.stride_u,
              converted.data + converted.stride_y * converted.height +
                  converted.stride_u * (converted.height / 2), converted.stride_v,
              converted.width, converted.height, small.data, kWidth,
              small.data + kWidth * kHeight, kWidth / 2,
              small.data + kWidth * kHeight * 5 / 4, kWidth / 2,
              kWidth, kHeight, libyuv::kFilterBox) != 0) {
        throw std::runtime_error("Direct camera I420 resize failed");
      }
      const auto wall_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
          std::chrono::system_clock::now().time_since_epoch()).count();
        const int64_t age_us = SteadyMicros() - raw.timestamp_us;
        if (age_us < 0 || age_us > 200000) continue;
        const uint64_t timestamp_ns = wall_ns - age_us * 1000;
      if (input.writer) input.writer->Publish(small.data, small.total(), kWidth, kHeight,
                                             timestamp_ns, input.name);
      {
        std::lock_guard<std::mutex> lock(mutex_);
        input.latest = std::move(small);
        input.timestamp_us = raw.timestamp_us;
        ++received_;
      }
      ready_.notify_all();
    }
  } catch (const std::exception& error) {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      error_ = error.what();
    }
    ready_.notify_all();
  }
}

bool DirectImageSource::TakeI420(cv::Mat* output, std::chrono::milliseconds timeout) {
  if (!output) throw std::invalid_argument("Missing I420 output");
  std::unique_lock<std::mutex> lock(mutex_);
  if (!ready_.wait_for(lock, timeout, [this] {
        if (stopped_ || !error_.empty()) return true;
        for (const auto& input : inputs_) if (input->latest.empty()) return false;
        return true;
      })) return false;
  if (stopped_) return false;
  if (!error_.empty()) throw std::runtime_error(error_);
  for (auto& input : inputs_) {
    if (SteadyMicros() - input->timestamp_us > 200000) {
      input->latest.release();
      return false;
    }
  }
  if (inputs_.size() == 2) {
    const int64_t delta = inputs_[0]->timestamp_us - inputs_[1]->timestamp_us;
    if (std::abs(delta) > 30000) {
      inputs_[delta < 0 ? 0 : 1]->latest.release();
      return false;
    }
    *output = StitchI420(inputs_[0]->latest, inputs_[1]->latest);
    for (auto& input : inputs_) input->latest.release();
  } else {
    *output = std::move(inputs_[0]->latest);
  }
  return true;
}

}