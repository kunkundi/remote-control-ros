#include "fused_detection_view.h"

#include <cstdlib>
#include <iostream>
#include <limits>
#include <string>

using rtc_console::FusedDetectionView;
using namespace std::chrono_literals;

void Check(bool condition, const char* message) {
  if (!condition) {
    std::cerr << message << '\n';
    std::exit(EXIT_FAILURE);
  }
}

int main() {
  FusedDetectionView view;
  const auto now = FusedDetectionView::Clock::now();
  FusedDetectionView::Snapshot snapshot;
  snapshot.set_sequence(7);
  snapshot.set_capture_time_ns(123456789012345);
  snapshot.set_frame_id("base_link");
  snapshot.set_frame_seq(55);
  auto* radar = snapshot.add_targets();
  radar->set_has_3d(true);
  radar->set_class_id(std::numeric_limits<uint32_t>::max());
  radar->set_radar_source_id("front_left");
  radar->set_radar_object_id(255);
  radar->set_position_x(10.5f);
  radar->set_velocity_y(-2.5f);
  auto* camera = snapshot.add_targets();
  camera->set_camera_source_id("front_right");
  camera->set_width(0.25f);
  const std::string bytes = snapshot.SerializeAsString();
  Check(view.Update(bytes.data(), bytes.size(), now), "accept fused snapshot");
  const auto latest = view.Get(now);
  Check(latest && latest->SerializeAsString() == bytes, "preserve all serialized fields");
  Check(!view.Update(bytes.data(), bytes.size(), now + 400ms), "reject duplicate");
  Check(!view.Update("\x80", 1, now), "reject truncated protobuf");
  Check(!view.Update(bytes.data(), dozer::perception::v1::kMaxFusedSnapshotBytes + 1, now),
        "reject oversized payload before parsing");
  Check(view.Get(now + 500ms) && !view.Get(now + 501ms), "expire by reception time");
  snapshot.Clear();
  snapshot.set_sequence(6);
  auto empty = snapshot.SerializeAsString();
  Check(!view.Update(empty.data(), empty.size(), now + 600ms), "reject stale sequence after expiry");
  snapshot.set_sequence(8);
  empty = snapshot.SerializeAsString();
  Check(view.Update(empty.data(), empty.size(), now + 600ms) &&
        view.Get(now + 600ms)->targets().empty(), "new empty snapshot clears targets");
  view = FusedDetectionView{};
  Check(!view.Get(now) && view.Update(bytes.data(), bytes.size(), now), "new channel resets sequence");
}