#pragma once

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <vector>
#include <libyuv/convert_argb.h>

namespace dozer_camera_gmsl {

class Yuv422Downsampler
{
public:
  void Convert(const uint8_t * source, size_t source_size, uint32_t width,
    uint32_t height, uint32_t stride, uint32_t scale, bool yuyv,
    uint8_t * destination, size_t destination_size)
  {
    if (!source || !destination || width < 2 || width % 2 || !height || !scale ||
      stride < static_cast<size_t>(width) * 2 ||
      source_size < static_cast<size_t>(stride) * height ||
      !(width / scale) || !(height / scale) ||
      destination_size < static_cast<size_t>(width / scale) * (height / scale) * 3)
    {
      throw std::invalid_argument("Invalid YUV422 image layout");
    }
    const uint32_t output_width = width / scale;
    const uint32_t output_height = height / scale;
    const size_t pixels = static_cast<size_t>(output_width) * output_height;
    packed_row_.resize(static_cast<size_t>(width) * 2);
    luma_.resize(pixels);
    chroma_u_.resize(pixels);
    chroma_v_.resize(pixels);
    for (uint32_t row = 0; row < height / scale; ++row) {
      std::memcpy(packed_row_.data(), source + static_cast<size_t>(row) * scale * stride,
        static_cast<size_t>(width) * 2);
      for (uint32_t column = 0; column < output_width; ++column) {
        const uint32_t source_column = column * scale;
        const auto * pair = packed_row_.data() + (source_column / 2) * 4;
        const size_t offset = static_cast<size_t>(row) * output_width + column;
        luma_[offset] = std::max<uint8_t>(16,
          pair[(source_column & 1) * 2 + (yuyv ? 0 : 1)]);
        chroma_u_[offset] = pair[yuyv ? 1 : 0];
        chroma_v_[offset] = pair[yuyv ? 3 : 2];
      }
    }
    if (libyuv::I444ToRGB24(luma_.data(), output_width, chroma_u_.data(), output_width,
        chroma_v_.data(), output_width, destination, output_width * 3,
        output_width, output_height) != 0)
    {
      throw std::runtime_error("YUV to BGR conversion failed");
    }
  }

private:
  std::vector<uint8_t> packed_row_;
  std::vector<uint8_t> luma_;
  std::vector<uint8_t> chroma_u_;
  std::vector<uint8_t> chroma_v_;
};

}