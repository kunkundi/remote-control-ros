#pragma once

#include <fcntl.h>
#include <sys/file.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include <chrono>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

namespace rtc_edge {

struct SharedCameraHeader {
  char magic[8];
  uint32_t version;
  uint32_t header_size;
  uint64_t sequence;
  uint64_t timestamp_ns;
  uint64_t published_monotonic_ns;
  uint32_t width;
  uint32_t height;
  uint32_t payload_size;
  uint32_t reserved;
  char frame_id[32];
};

static_assert(sizeof(SharedCameraHeader) == 88, "Shared camera ABI changed");

class SharedCameraWriter {
 public:
  SharedCameraWriter(const std::string& name, size_t capacity)
      : capacity_(capacity) {
    if (name.empty() || name.find_first_not_of(
            "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_") != std::string::npos ||
        capacity == 0 || capacity > 64 * 1024 * 1024) {
      throw std::invalid_argument("Invalid shared camera name or capacity");
    }
    path_ = "/dev/shm/" + name;
    owner_fd_ = open((path_ + ".owner").c_str(), O_CREAT | O_RDWR | O_CLOEXEC | O_NOFOLLOW, 0600);
    if (owner_fd_ < 0 || flock(owner_fd_, LOCK_EX | LOCK_NB) != 0) {
      if (owner_fd_ >= 0) close(owner_fd_);
      throw std::runtime_error("Shared camera already owned or unavailable: " + name);
    }
    std::string temporary = path_ + ".XXXXXX";
    std::vector<char> filename(temporary.begin(), temporary.end());
    filename.push_back('\0');
    fd_ = mkstemp(filename.data());
    if (fd_ >= 0) fcntl(fd_, F_SETFD, FD_CLOEXEC);
    const size_t size = sizeof(SharedCameraHeader) + capacity_;
    if (fd_ >= 0 && ftruncate(fd_, static_cast<off_t>(size)) == 0) {
      mapping_ = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd_, 0);
    }
    if (mapping_ == MAP_FAILED || rename(filename.data(), path_.c_str()) != 0) {
      if (mapping_ != MAP_FAILED) munmap(mapping_, size);
      if (fd_ >= 0) close(fd_);
      unlink(filename.data());
      close(owner_fd_);
      throw std::runtime_error("Cannot create shared camera: " + name);
    }
  }

  ~SharedCameraWriter() {
    unlink(path_.c_str());
    munmap(mapping_, sizeof(SharedCameraHeader) + capacity_);
    close(fd_);
    close(owner_fd_);
  }

  SharedCameraWriter(const SharedCameraWriter&) = delete;
  SharedCameraWriter& operator=(const SharedCameraWriter&) = delete;

  bool Publish(const uint8_t* i420, size_t size, uint32_t width, uint32_t height,
               uint64_t timestamp_ns, const std::string& frame_id) {
    if (!i420 || width == 0 || height == 0 || width % 2 || height % 2 ||
        size != static_cast<uint64_t>(width) * height * 3 / 2 ||
        size > capacity_ || frame_id.empty() || frame_id.size() >= 32) {
      throw std::invalid_argument("Invalid shared I420 frame");
    }
    if (flock(fd_, LOCK_EX | LOCK_NB) != 0) return false;
    SharedCameraHeader header{};
    std::memcpy(header.magic, "DZRI420", 7);
    header.version = 1;
    header.header_size = sizeof(header);
    header.sequence = ++sequence_;
    header.timestamp_ns = timestamp_ns;
    header.published_monotonic_ns = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());
    header.width = width;
    header.height = height;
    header.payload_size = static_cast<uint32_t>(size);
    std::memcpy(header.frame_id, frame_id.data(), frame_id.size());
    std::memcpy(static_cast<uint8_t*>(mapping_) + sizeof(header), i420, size);
    std::memcpy(mapping_, &header, sizeof(header));
    flock(fd_, LOCK_UN);
    return true;
  }

 private:
  std::string path_;
  size_t capacity_;
  int owner_fd_ = -1;
  int fd_ = -1;
  void* mapping_ = MAP_FAILED;
  uint64_t sequence_ = 0;
};

}  // namespace rtc_edge