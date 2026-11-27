#include "video/decode/nvdec_factory.h"

#include <api/video_codecs/sdp_video_format.h>
#include <api/video_codecs/video_decoder.h>
#include <cstdlib>
#include <iostream>
#include <string>

namespace {
void Check(bool condition, const char* message) {
  if (!condition) {
    std::cerr << message << std::endl;
    std::exit(1);
  }
}

class FrameCheck final : public webrtc::DecodedImageCallback {
 public:
  int32_t Decoded(webrtc::VideoFrame& frame) override {
    Check(frame.width() == width && frame.height() == 64, "unexpected decoded dimensions");
    Check(frame.timestamp() == timestamp, "lost RTP timestamp");
    const auto pixels = frame.video_frame_buffer()->ToI420();
    Check(pixels && std::abs(pixels->DataY()[0] - 81) <= 2 &&
          std::abs(pixels->DataU()[0] - 90) <= 2 && std::abs(pixels->DataV()[0] - 240) <= 2,
          "unexpected decoded red pixel in I420 planes");
    ++received;
    return 0;
  }
  int width = 64;
  uint32_t timestamp = 0xffff0000u;
  int received = 0;
};

void DecodeRedFrame(webrtc::VideoDecoder& decoder, FrameCheck& check, const char* data, size_t size) {
  webrtc::EncodedImage image;
  image.SetEncodedData(webrtc::EncodedImageBuffer::Create(reinterpret_cast<const uint8_t*>(data), size));
  image.SetTimestamp(check.timestamp);
  image._encodedWidth = check.width;
  image._encodedHeight = 64;
  image._frameType = webrtc::VideoFrameType::kVideoFrameKey;
  const int before = check.received;
  Check(decoder.Decode(image, false, 0) == 0 && check.received == before + 1,
        "NVDEC did not return the H.264 access unit as a frame");
}
}

int main() {
  auto factory = webrtc::CreateNvdecH264Factory();
  auto formats = factory->GetSupportedFormats();
  Check(!formats.empty() && !factory->CreateVideoDecoder(webrtc::SdpVideoFormat("VP8")),
        "NVDEC factory must negotiate only H.264");
  auto decoder = factory->CreateVideoDecoder(formats.front());
  Check(decoder && std::string(decoder->ImplementationName()) == "NvDecH264", "wrong decoder");
  FrameCheck check;
  webrtc::VideoCodec codec{};
  codec.codecType = webrtc::kVideoCodecH264;
  codec.width = 64;
  codec.height = 64;
  Check(decoder->InitDecode(&codec, 1) == 0, "NVDEC initialization failed; check GPU and NVIDIA driver");
  decoder->RegisterDecodeCompleteCallback(&check);
  const char red64[] =
      "\x00\x00\x00\x01\x67\x42\xc0\x0a\xd9\x04\x26\xc0\x44\x00\x00\x03\x00\x04\x00\x00\x03\x00\x0a\x3c\x48\x99\x20"
      "\x00\x00\x00\x01\x68\xcb\x83\xcb\x20\x00\x00\x01\x65\x88\x84\x04\xbc\x46\x28\x00\x0a\x8b\xc7\x00\x01\x28\xd8\xe0"
      "\x00\x2f\xad\x27\x27\x27\x5d\x75\xd7\x5d\x75\xd7\x5d\x75\xd7\x80";
  const char red96[] =
      "\x00\x00\x00\x01\x67\x42\xc0\x0a\xd9\x06\x26\xc0\x44\x00\x00\x03\x00\x04\x00\x00\x03\x00\x0a\x3c\x48\x99\x20"
      "\x00\x00\x00\x01\x68\xcb\x83\xcb\x20\x00\x00\x01\x65\x88\x84\x04\xbc\x46\x28\x00\x0a\x8b\xc7\x00\x01\x28\xd8\xe0"
      "\x00\x2f\xad\x27\x27\x27\x27\x27\x5d\x75\xd7\x5d\x75\xd7\x5d\x75\xd7\x5d\x75\xd7\x5d\x78";
  DecodeRedFrame(*decoder, check, red64, sizeof(red64) - 1);
  check.width = 96;
  check.timestamp += 3000;
  DecodeRedFrame(*decoder, check, red96, sizeof(red96) - 1);
  decoder->Release();
  std::cout << "NvDecH264 decoded both frames on the GPU\n";
}
