#pragma once

#include <opencv2/core.hpp>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace rtc_edge {

cv::Mat StitchI420(const cv::Mat& left, const cv::Mat& right);

class DirectImageSource {
 public:
  DirectImageSource(const std::vector<std::string>& devices,
                    const std::vector<std::string>& camera_names);
  ~DirectImageSource();
  void RequestStop();
  bool TakeI420(cv::Mat* output, std::chrono::milliseconds timeout);
  uint64_t received() const { return received_.load(); }

 private:
  struct Input;
  void Run(size_t index);
  std::vector<std::unique_ptr<Input>> inputs_;
  std::vector<std::thread> workers_;
  std::mutex mutex_;
  std::condition_variable ready_;
  std::atomic<bool> stopped_{false};
  std::atomic<uint64_t> received_{0};
  std::string error_;
};

}