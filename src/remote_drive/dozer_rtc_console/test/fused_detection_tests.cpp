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

    const auto update = [&](uint64_t sequence, const char* source, const char* camera,
            FusedDetectionView::Clock::time_point time) {
      FusedDetectionView::Snapshot message;
      message.set_sequence(sequence);
      message.set_source_id(source);
      if (camera) message.add_targets()->set_camera_source_id(camera);
      const auto payload = message.SerializeAsString();
      return view.Update(payload.data(), payload.size(), time);
    };
    Check(update(10, "camera:front_right", "front_right", now), "accept right update");
    Check(update(9, "camera:front_left", "front_left", now + 50ms), "order independently by camera");
    Check(view.GetOverlays(now + 50ms).size() == 2, "retain left and right after alternating updates");
    Check(!update(8, "camera:front_left", nullptr, now + 60ms), "reject older same-camera clear");
    Check(update(11, "radar_fusion", nullptr, now + 100ms), "accept periodic radar snapshot");
    Check(view.GetOverlays(now + 100ms).size() == 2, "periodic snapshot does not clear camera views");
    Check(update(12, "camera:front_left", nullptr, now + 200ms), "accept empty left update");
    const auto overlays = view.GetOverlays(now + 200ms);
    Check(overlays.size() == 2 && overlays[0]->targets().empty() &&
      overlays[1]->targets_size() == 1, "empty left update clears only left");
    const auto fresh = view.GetOverlays(now + 501ms);
    Check(fresh.size() == 1 && fresh.front()->source_id() == "camera:front_left",
      "right expires independently even with newer radar and left updates");
}