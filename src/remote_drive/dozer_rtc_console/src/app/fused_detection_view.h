#pragma once

#include <chrono>
#include <cstddef>
#include <memory>
#include <utility>

#include "fused_detection.pb.h"
#include "fused_detection_channel.h"

namespace rtc_console {

class FusedDetectionView {
 public:
  using Clock = std::chrono::steady_clock;
  using Snapshot = dozer::perception::v1::FusedSnapshot;

  bool Update(const char* data, std::size_t size, Clock::time_point received_at) {
    if (!data || size == 0 || size > dozer::perception::v1::kMaxFusedSnapshotBytes) {
      return false;
    }
    auto next = std::make_shared<Snapshot>();
    if (!next->ParseFromArray(data, static_cast<int>(size)) ||
        next->sequence() == 0 || (snapshot_ && next->sequence() <= snapshot_->sequence())) {
      return false;
    }
    snapshot_ = std::move(next);
    received_at_ = received_at;
    return true;
  }

  std::shared_ptr<const Snapshot> Get(Clock::time_point now) const {
    return now - received_at_ <= std::chrono::milliseconds(500) ? snapshot_ : nullptr;
  }

 private:
  std::shared_ptr<const Snapshot> snapshot_;
  Clock::time_point received_at_{};
};

}  // namespace rtc_console