#pragma once

#include <chrono>
#include <cstddef>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

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
    if (!next->ParseFromArray(data, static_cast<int>(size)) || next->sequence() == 0) {
      return false;
    }
    const std::string prefix = dozer::perception::v1::kCameraUpdatePrefix;
    TimedSnapshot* entry = &full_;
    if (next->source_id().compare(0, prefix.size(), prefix) == 0) {
      const std::string camera = next->source_id().substr(prefix.size());
      if (camera != "front_left" && camera != "front_right" && camera != "front" &&
          camera != "rear" && camera != "left" && camera != "right") {
        return false;
      }
      entry = &cameras_[camera];
    }
    if (entry->snapshot && next->sequence() <= entry->snapshot->sequence()) {
      return false;
    }
    *entry = {std::move(next), received_at};
    return true;
  }

  std::shared_ptr<const Snapshot> Get(Clock::time_point now) const {
    return full_.Get(now);
  }

  std::vector<std::shared_ptr<const Snapshot>> GetOverlays(Clock::time_point now) const {
    std::vector<std::shared_ptr<const Snapshot>> overlays;
    if (cameras_.empty()) {
      if (auto snapshot = Get(now)) overlays.push_back(std::move(snapshot));
      return overlays;
    }
    for (const auto& camera : cameras_) {
      if (auto snapshot = camera.second.Get(now)) overlays.push_back(std::move(snapshot));
    }
    return overlays;
  }

 private:
  struct TimedSnapshot {
    std::shared_ptr<const Snapshot> snapshot;
    Clock::time_point received_at{};

    std::shared_ptr<const Snapshot> Get(Clock::time_point now) const {
      return now - received_at <= std::chrono::milliseconds(500) ? snapshot : nullptr;
    }
  };

  TimedSnapshot full_;
  std::map<std::string, TimedSnapshot> cameras_;
};

}  // namespace rtc_console