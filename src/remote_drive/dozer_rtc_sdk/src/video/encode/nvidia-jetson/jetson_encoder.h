#ifndef JETSON_ENCODER_H_
#define JETSON_ENCODER_H_

#include <api/video/encoded_image.h>
#include <api/video/i420_buffer.h>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>

#include "/usr/src/jetson_multimedia_api/include/NvVideoEncoder.h"
#include "/usr/src/jetson_multimedia_api/include/nvbufsurface.h"

#ifndef ENABLE_ENCODE_PERF_STATS
#define ENABLE_ENCODE_PERF_STATS 0
#endif

namespace webrtc {

class JetsonEncoder {
 public:
  struct StrategyConfig {
    enum v4l2_mpeg_video_bitrate_mode bitrate_mode =
        V4L2_MPEG_VIDEO_BITRATE_MODE_CBR;
    uint32_t gop_size = 3000;
    bool has_qp_range = false;
    uint32_t qp_min = 0;
    uint32_t qp_max = 0;
  };

  JetsonEncoder(int width, int height, uint32_t dst_pix_fmt, bool is_dma_src);
  ~JetsonEncoder();

  static std::unique_ptr<JetsonEncoder> Create(int width, int height,
                                                uint32_t dst_pix_fmt,
                                                bool is_dma_src,
                                                const StrategyConfig& config,
                                                int framerate,
                                                int bitrate_bps);

  using CaptureCallback = std::function<void(
      const rtc::scoped_refptr<EncodedImageBuffer>&, bool, uint64_t)>;
  void EmplaceBuffer(rtc::scoped_refptr<I420BufferInterface> i420_buffer,
                     CaptureCallback on_capture);
  void SetStrategyConfig(const StrategyConfig& config);
  void ForceKeyFrame();
  void SetBitrate(int bitrate_bps);
  void SetFramerate(int framerate);
  int framerate() const { return framerate_; }
  bool Reconfigure(int new_width, int new_height);
  bool CanReconfigure() const;
  bool IsHealthy() const;
  int width() const { return width_; }
  int height() const { return height_; }

 private:
  NvVideoEncoder* encoder_;
  std::once_flag start_capture_thread_;
  bool capture_thread_started_ = false;
  std::atomic<bool> abort_;
  std::atomic<bool> stopping_;
  std::atomic<bool> awaiting_resolution_idr_;
  std::atomic<bool> force_idr_before_next_frame_;
  std::atomic<bool> reconfigure_ready_;
  std::atomic<int64_t> resolution_deadline_ms_{0};
  int width_;
  int height_;
  int session_width_;
  int session_height_;
  int framerate_;
  int bitrate_bps_;
  uint32_t applied_peak_bitrate_ = 0;
  int64_t next_bitrate_retry_ms_ = 0;
  int64_t next_framerate_retry_ms_ = 0;
  uint32_t src_pix_fmt_;
  uint32_t dst_pix_fmt_;
  bool is_dma_src_;

  struct CaptureTask {
    uint64_t timestamp_us = 0;
    CaptureCallback callback;
#if ENABLE_ENCODE_PERF_STATS
    int64_t encode_start_time_us;  // 编码开始时间（微秒）
#endif
  };

  std::deque<CaptureTask> capturing_tasks_;
  std::mutex tasks_mutex_;
  std::condition_variable tasks_condition_;
  std::atomic<uint64_t> next_task_timestamp_us_{1};

  std::deque<uint32_t> available_output_buffers_;
  std::mutex output_buffers_mutex_;
  std::condition_variable output_buffers_condition_;
  std::atomic<int64_t> last_output_drop_log_ms_{0};

  StrategyConfig strategy_config_;

  bool CreateVideoEncoder();
  bool ApplyCodecSettings();
  bool PrepareCaptureBuffer();
  void ResetAvailableOutputBuffers();
  bool ReclaimOutputBuffer();
  bool Start();
  void StopEncoderIo();
  void LogQueueState(const char* event);
  bool DrainOutputPlane();
  bool WaitForPendingTasks();
  void MarkUnhealthyAfterReconfigureFailure(const char* stage, int ret);
  static bool EncoderCapturePlaneDqCallback(struct v4l2_buffer* v4l2_buf,
                                            NvBuffer* buffer,
                                            NvBuffer* shared_buffer, void* arg);
  bool ConvertI420ToYUV420M(
      NvBuffer* nv_buffer,
      rtc::scoped_refptr<I420BufferInterface> i420_buffer);
};

}  // namespace webrtc

#endif  // JETSON_ENCODER_H_
