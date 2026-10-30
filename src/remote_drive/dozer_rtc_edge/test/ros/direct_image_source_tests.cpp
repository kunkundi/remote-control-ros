#include "dozer_rtc_edge/direct_image_source.h"
#include "rtc_camera/single/async_image_source.h"
#include "rtc_camera/single/frame_converter.h"
#include <linux/videodev2.h>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

void TestCudaConversion() {
  for (const auto format : {V4L2_PIX_FMT_YUYV, V4L2_PIX_FMT_UYVY, V4L2_PIX_FMT_NV12}) {
    std::vector<uint8_t> pixels;
    if (format == V4L2_PIX_FMT_NV12) {
      pixels.assign(16, 81);
      for (int pair = 0; pair < 4; ++pair) pixels.insert(pixels.end(), {90, 240});
    } else {
      for (int pair = 0; pair < 8; ++pair) {
        if (format == V4L2_PIX_FMT_YUYV) pixels.insert(pixels.end(), {81, 90, 81, 240});
        else pixels.insert(pixels.end(), {90, 81, 240, 81});
      }
    }
    rtc_camera::single::CameraFrame raw;
    raw.width = 4;
    raw.height = 4;
    raw.pixel_format = format;
    raw.stride_bytes = format == V4L2_PIX_FMT_NV12 ? 4 : 8;
    raw.data = pixels.data();
    raw.data_size = pixels.size();
    rtc_camera::single::CameraFrameConverter converter;
    rtc_camera::single::ConvertedCameraFrame converted;
    std::string error;
    if (!converter.ConvertToI420(raw, &converted, &error)) throw std::runtime_error(error);
    if (converted.width != 4 || converted.height != 4) {
      throw std::runtime_error("CUDA conversion changed dimensions");
    }
    for (int index = 0; index < 24; ++index) {
      const uint8_t expected = index < 16 ? 81 : (index < 20 ? 90 : 240);
      if (converted.data[index] != expected) throw std::runtime_error("CUDA I420 pixel mismatch");
    }
  }
  std::cout << "CUDA YUYV/UYVY/NV12 conversion tests passed\n";
}

int main(int argc, char** argv) {
  if (argc == 2 && std::string(argv[1]) == "--cuda") TestCudaConversion();
  cv::Mat left(6, 4, CV_8UC1, cv::Scalar(0));
  cv::Mat right(6, 4, CV_8UC1, cv::Scalar(0));
  for (size_t index = 0; index < left.total(); ++index) {
    left.data[index] = index;
    right.data[index] = index + 100;
  }
  const cv::Mat output = rtc_edge::StitchI420(left, right);
  size_t input_offset = 0;
  size_t output_offset = 0;
  for (int plane = 0; plane < 3; ++plane) {
    const size_t width = plane == 0 ? 4 : 2;
    const int rows = plane == 0 ? 4 : 2;
    for (int row = 0; row < rows; ++row) {
      for (size_t column = 0; column < width; ++column) {
        if (output.data[output_offset + column] != left.data[input_offset + column] ||
            output.data[output_offset + width + column] != right.data[input_offset + column]) {
          throw std::runtime_error("Stereo plane layout mismatch");
        }
      }
      input_offset += width;
      output_offset += width * 2;
    }
  }
  bool rejected = false;
  try { rtc_edge::StitchI420(left, cv::Mat(6, 6, CV_8UC1)); }
  catch (const std::invalid_argument&) { rejected = true; }
  if (!rejected) throw std::runtime_error("Mismatched stereo dimensions accepted");
  std::cout << "Direct camera I420 stitch tests passed\n";
}