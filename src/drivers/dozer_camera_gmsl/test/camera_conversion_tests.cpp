#include "dozer_camera_gmsl/yuv422_downsampler.hpp"

#include <algorithm>
#include <chrono>
#include <iostream>
#include <random>
#include <vector>

void Require(bool condition, const char * message)
{
  if (!condition) {throw std::runtime_error(message);}
}

void Reference(const std::vector<uint8_t> & source, uint32_t width, uint32_t height,
  uint32_t stride, uint32_t scale, bool yuyv, std::vector<uint8_t> & output)
{
  for (uint32_t row = 0; row < height / scale; ++row) {
    for (uint32_t column = 0; column < width / scale; ++column) {
      const uint32_t source_column = column * scale;
      const auto * pair = source.data() + static_cast<size_t>(row) * scale * stride +
        (source_column / 2) * 4;
      const int luma = yuyv ? pair[(source_column & 1) ? 2 : 0] :
        pair[(source_column & 1) ? 3 : 1];
      const int chroma_u = (yuyv ? pair[1] : pair[0]) - 128;
      const int chroma_v = (yuyv ? pair[3] : pair[2]) - 128;
      const int luminance = std::max(0, luma - 16);
      const size_t offset = (static_cast<size_t>(row) * (width / scale) + column) * 3;
      output[offset] = std::clamp((298 * luminance + 516 * chroma_u + 128) >> 8, 0, 255);
      output[offset + 1] = std::clamp(
        (298 * luminance - 100 * chroma_u - 208 * chroma_v + 128) >> 8, 0, 255);
      output[offset + 2] = std::clamp((298 * luminance + 409 * chroma_v + 128) >> 8, 0, 255);
    }
  }
}

int main()
{
  try {
    std::mt19937 random(42);
    dozer_camera_gmsl::Yuv422Downsampler converter;
    for (const uint32_t width : {14u, 1920u}) {
      const uint32_t height = width == 14 ? 10 : 1080;
      const uint32_t stride = width * 2 + 16;
      std::vector<uint8_t> source(static_cast<size_t>(stride) * height);
      for (auto & value : source) {value = random() % 256;}
      for (const uint32_t scale : {1u, 2u, 3u}) {
        for (const bool yuyv : {true, false}) {
          const size_t size = static_cast<size_t>(width / scale) * (height / scale) * 3;
          std::vector<uint8_t> actual(size), expected(size);
          Reference(source, width, height, stride, scale, yuyv, expected);
          converter.Convert(source.data(), source.size(), width, height, stride,
            scale, yuyv, actual.data(), actual.size());
          for (size_t index = 0; index < size; ++index) {
            Require(std::abs(int(actual[index]) - int(expected[index])) <= 2,
              "SIMD conversion changed color, channel order or sampling position");
          }
        }
      }
    }
    std::vector<uint8_t> source(1920 * 1080 * 2, 128), output(640 * 360 * 3);
    bool rejected = false;
    try {
      converter.Convert(source.data(), 1, 1920, 1080, 3840, 3, true,
        output.data(), output.size());
    } catch (const std::invalid_argument &) {rejected = true;}
    Require(rejected, "Truncated input was accepted");
    const auto start = std::chrono::steady_clock::now();
    for (int frame = 0; frame < 60; ++frame) {
      Reference(source, 1920, 1080, 3840, 3, true, output);
    }
    const auto middle = std::chrono::steady_clock::now();
    for (int frame = 0; frame < 60; ++frame) {
      converter.Convert(source.data(), source.size(), 1920, 1080, 3840, 3, true,
        output.data(), output.size());
    }
    const auto end = std::chrono::steady_clock::now();
    std::cout << "Cached-buffer conversion ms/frame: scalar=" <<
      std::chrono::duration<double, std::milli>(middle - start).count() / 60 <<
      " SIMD=" << std::chrono::duration<double, std::milli>(end - middle).count() / 60 << '\n';
    std::cout << "Camera conversion tests passed\n";
  } catch (const std::exception & error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}