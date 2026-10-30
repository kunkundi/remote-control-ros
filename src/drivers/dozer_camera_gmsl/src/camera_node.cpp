#include <fcntl.h>
#include <linux/videodev2.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include "dozer_camera_gmsl/frame_rate_limiter.hpp"
#include "dozer_camera_gmsl/yuv422_downsampler.hpp"
#include "rclcpp/rclcpp.hpp"
#include "sensor_msgs/msg/image.hpp"

namespace {

int RetryIoctl(int fd, unsigned long request, void * arg)
{
  int result;
  do {
    result = ioctl(fd, request, arg);
  } while (result < 0 && errno == EINTR);
  return result;
}

std::runtime_error DeviceError(const std::string & operation, const std::string & device)
{
  return std::runtime_error(operation + " " + device + ": " + std::strerror(errno));
}

struct MappedBuffer
{
  void * data = nullptr;
  size_t length = 0;
};

class CameraNode : public rclcpp::Node
{
public:
  CameraNode()
  : Node("camera_gmsl")
  {
    name_ = declare_parameter<std::string>("camera_name", "front_left");
    device_ = declare_parameter<std::string>("device", "/dev/video2");
    const int width = declare_parameter<int>("width", 1280);
    const int height = declare_parameter<int>("height", 720);
    const int downscale = declare_parameter<int>("small_image_downscale", 2);
    const double max_fps = declare_parameter<double>("small_image_max_fps", 30.0);
    if (name_.empty() || width <= 0 || height <= 0 || downscale <= 0 || max_fps <= 0) {
      throw std::invalid_argument("camera_name, width, height, downscale and max_fps must be positive");
    }
    requested_width_ = static_cast<uint32_t>(width);
    requested_height_ = static_cast<uint32_t>(height);
    downscale_ = static_cast<uint32_t>(downscale);
    frame_rate_limiter_ = dozer_camera_gmsl::FrameRateLimiter(max_fps);
    publisher_ = create_publisher<sensor_msgs::msg::Image>(
      "/camera/" + name_ + "/image_small", rclcpp::SensorDataQoS());
    OpenCamera();
    worker_ = std::thread([this]() {CaptureLoop();});
  }

  ~CameraNode() override
  {
    running_ = false;
    if (worker_.joinable()) {
      worker_.join();
    }
    CloseCamera();
  }

private:
  void OpenCamera()
  {
    fd_ = open(device_.c_str(), O_RDWR | O_NONBLOCK);
    if (fd_ < 0) {
      throw DeviceError("open", device_);
    }
    try {
      v4l2_capability caps{};
      if (RetryIoctl(fd_, VIDIOC_QUERYCAP, &caps) < 0) {
        throw DeviceError("VIDIOC_QUERYCAP", device_);
      }
      const uint32_t flags = (caps.capabilities & V4L2_CAP_DEVICE_CAPS) ?
        caps.device_caps : caps.capabilities;
      if (!(flags & V4L2_CAP_VIDEO_CAPTURE) || !(flags & V4L2_CAP_STREAMING)) {
        throw std::runtime_error(device_ + " does not support V4L2 capture and streaming");
      }

      // Like rtc-framework's V4l2CameraDevice, negotiate the format before mmap.
      bool accepted = false;
      int format_error = 0;
      for (const uint32_t pixel_format : {V4L2_PIX_FMT_YUYV, V4L2_PIX_FMT_UYVY}) {
        v4l2_format requested{};
        requested.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        requested.fmt.pix.width = requested_width_;
        requested.fmt.pix.height = requested_height_;
        requested.fmt.pix.pixelformat = pixel_format;
        requested.fmt.pix.field = V4L2_FIELD_ANY;
        const int result = RetryIoctl(fd_, VIDIOC_S_FMT, &requested);
        if (result < 0) {
          format_error = errno;
        }
        if (result == 0 &&
          requested.fmt.pix.width == requested_width_ &&
          requested.fmt.pix.height == requested_height_ &&
          requested.fmt.pix.pixelformat == pixel_format)
        {
          format_ = requested;
          accepted = true;
          break;
        }
      }
      if (!accepted) {
        if (format_error == EBUSY) {
          throw std::runtime_error(device_ + " is busy; stop the process currently capturing it");
        }
        throw std::runtime_error(device_ + " cannot provide requested YUYV/UYVY resolution");
      }
      stride_ = format_.fmt.pix.bytesperline ? format_.fmt.pix.bytesperline : requested_width_ * 2;
      if (stride_ < requested_width_ * 2 || requested_width_ < 2) {
        throw std::runtime_error(device_ + " returned invalid row stride or width");
      }

      v4l2_requestbuffers request{};
      request.count = 4;
      request.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
      request.memory = V4L2_MEMORY_MMAP;
      if (RetryIoctl(fd_, VIDIOC_REQBUFS, &request) < 0) {
        throw DeviceError("VIDIOC_REQBUFS", device_);
      }
      if (request.count < 2) {
        throw std::runtime_error(device_ + " returned fewer than two buffers");
      }
      buffers_.resize(request.count);
      for (uint32_t i = 0; i < request.count; ++i) {
        v4l2_buffer buffer{};
        buffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buffer.memory = V4L2_MEMORY_MMAP;
        buffer.index = i;
        if (RetryIoctl(fd_, VIDIOC_QUERYBUF, &buffer) < 0) {
          throw DeviceError("VIDIOC_QUERYBUF", device_);
        }
        void * address = mmap(nullptr, buffer.length, PROT_READ | PROT_WRITE,
          MAP_SHARED, fd_, buffer.m.offset);
        if (address == MAP_FAILED) {
          throw DeviceError("mmap", device_);
        }
        buffers_[i] = {address, buffer.length};
        if (RetryIoctl(fd_, VIDIOC_QBUF, &buffer) < 0) {
          throw DeviceError("VIDIOC_QBUF", device_);
        }
      }
      v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
      if (RetryIoctl(fd_, VIDIOC_STREAMON, &type) < 0) {
        throw DeviceError("VIDIOC_STREAMON", device_);
      }
      streaming_ = true;
      RCLCPP_INFO(get_logger(), "%s: %s %ux%u %s -> /camera/%s/image_small",
        name_.c_str(), device_.c_str(), requested_width_, requested_height_,
        format_.fmt.pix.pixelformat == V4L2_PIX_FMT_YUYV ? "YUYV" : "UYVY", name_.c_str());
    } catch (...) {
      CloseCamera();
      throw;
    }
  }

  void CloseCamera()
  {
    if (fd_ >= 0 && streaming_) {
      v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
      RetryIoctl(fd_, VIDIOC_STREAMOFF, &type);
      streaming_ = false;
    }
    for (const auto & buffer : buffers_) {
      if (buffer.data) {
        munmap(buffer.data, buffer.length);
      }
    }
    buffers_.clear();
    if (fd_ >= 0) {
      close(fd_);
      fd_ = -1;
    }
  }

  sensor_msgs::msg::Image MakeSmallImage(const uint8_t * source, size_t bytes_used)
  {
    const uint32_t width = requested_width_ / downscale_;
    const uint32_t height = requested_height_ / downscale_;
    if (width == 0 || height == 0 || bytes_used < static_cast<size_t>(stride_) * requested_height_) {
      throw std::runtime_error(device_ + " returned a short frame or invalid downscale");
    }
    sensor_msgs::msg::Image image;
    image.header.stamp = now();
    image.header.frame_id = "camera_" + name_;
    image.height = height;
    image.width = width;
    image.encoding = "bgr8";
    image.is_bigendian = false;
    image.step = width * 3;
    image.data.resize(static_cast<size_t>(image.step) * height);
    converter_.Convert(source, bytes_used, requested_width_, requested_height_, stride_,
      downscale_, format_.fmt.pix.pixelformat == V4L2_PIX_FMT_YUYV,
      image.data.data(), image.data.size());
    return image;
  }

  void CaptureLoop()
  {
    while (running_ && rclcpp::ok()) {
      pollfd descriptor{fd_, POLLIN, 0};
      const int ready = poll(&descriptor, 1, 1000);
      if (ready == 0) {
        RCLCPP_WARN_THROTTLE(get_logger(), *get_clock(), 5000,
          "%s: no camera frames from %s", name_.c_str(), device_.c_str());
        continue;
      }
      if (ready < 0) {
        if (errno == EINTR) {continue;}
        RCLCPP_ERROR(get_logger(), "%s: poll failed: %s", name_.c_str(), std::strerror(errno));
        break;
      }
      v4l2_buffer buffer{};
      buffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
      buffer.memory = V4L2_MEMORY_MMAP;
      if (RetryIoctl(fd_, VIDIOC_DQBUF, &buffer) < 0) {
        if (errno == EAGAIN) {continue;}
        RCLCPP_ERROR(get_logger(), "%s: dequeue failed: %s", name_.c_str(), std::strerror(errno));
        break;
      }
      if (buffer.index >= buffers_.size()) {
        RCLCPP_ERROR(get_logger(), "%s: invalid V4L2 buffer index", name_.c_str());
        break;
      }
      if (buffer.flags & V4L2_BUF_FLAG_ERROR) {
        RCLCPP_WARN_THROTTLE(get_logger(), *get_clock(), 5000,
          "%s: V4L2 returned an error frame from %s", name_.c_str(), device_.c_str());
      } else if (frame_rate_limiter_.ShouldPublish(std::chrono::steady_clock::now())) {
        try {
          auto image = MakeSmallImage(
            static_cast<const uint8_t *>(buffers_[buffer.index].data), buffer.bytesused);
          publisher_->publish(std::move(image));
        } catch (const std::exception & error) {
          RCLCPP_WARN_THROTTLE(get_logger(), *get_clock(), 5000,
            "%s: %s", name_.c_str(), error.what());
        }
      }
      if (RetryIoctl(fd_, VIDIOC_QBUF, &buffer) < 0) {
        RCLCPP_ERROR(get_logger(), "%s: requeue failed: %s", name_.c_str(), std::strerror(errno));
        break;
      }
    }
  }

  std::string name_;
  std::string device_;
  uint32_t requested_width_ = 0;
  uint32_t requested_height_ = 0;
  uint32_t downscale_ = 2;
  uint32_t stride_ = 0;
  dozer_camera_gmsl::FrameRateLimiter frame_rate_limiter_{30.0};
  dozer_camera_gmsl::Yuv422Downsampler converter_;
  v4l2_format format_{};
  int fd_ = -1;
  bool streaming_ = false;
  std::vector<MappedBuffer> buffers_;
  rclcpp::Publisher<sensor_msgs::msg::Image>::SharedPtr publisher_;
  std::atomic<bool> running_{true};
  std::thread worker_;
};

}  // namespace

int main(int argc, char ** argv)
{
  rclcpp::init(argc, argv);
  try {
    auto node = std::make_shared<CameraNode>();
    rclcpp::spin(node);
  } catch (const std::exception & error) {
    RCLCPP_FATAL(rclcpp::get_logger("camera_gmsl"), "%s", error.what());
    rclcpp::shutdown();
    return 1;
  }
  rclcpp::shutdown();
  return 0;
}
