#include <c_rtc.h>
#include "rtc_vision/view_control_protocol.h"

#include <atomic>
#include <chrono>
#include <cstring>
#include <iostream>
#include <thread>

namespace {
using namespace std::chrono_literals;
char room[] = "edge-reconnect-test";
std::atomic<bool> logged_in{false};
std::atomic<int> closed_rooms{0};
std::atomic<int> stereo_frames{0};
std::atomic<int> grid_frames{0};
std::atomic<int> front_frames{0};
std::atomic<RtcSessionId> view_peer{0};

template <typename Predicate>
bool WaitFor(Predicate ready) {
  const auto deadline = std::chrono::steady_clock::now() + 8s;
  while (!ready()) {
    if (std::chrono::steady_clock::now() >= deadline) return false;
    std::this_thread::sleep_for(10ms);
  }
  return true;
}
}  // namespace

int main() {
  RtcInitParams params{};
  params.config_filepath = "rtc.cfg";
  params.serverconnection_state_handler = [](RtcServerConnectionState state) {
    logged_in = state == ServerLogined;
  };
  params.room_handler = [](RtcRoomOperation op, RtcRoomId name) {
    if (op == RoomDelete && name && std::strcmp(name, room) == 0) ++closed_rooms;
  };
  params.datachannel_state_handler = [](RtcSessionId peer, RtcDataChannelLabel label,
                                        RtcDataChannelState state) {
    if (label && std::strcmp(label, vts_rtc::vision::kVideoViewControlChannelLabel) == 0) {
      view_peer = state == DataChannelOpen ? peer : 0;
    }
  };
  params.recv_frame_handler = [](RtcSessionId, RtcVideoSourceId source, RtcMediaSourceType,
                                 size_t width, size_t height, size_t, const unsigned char* data, size_t size) {
    if (!source || !width || !height || !data || !size) return;
    if (std::strcmp(source, "stereo") == 0) ++stereo_frames;
    if (std::strcmp(source, "surround_grid") == 0) ++grid_frames;
    if (std::strcmp(source, "surround_front") == 0) ++front_frames;
  };
  if (RtcInitAgentV2(params) != OK) return 1;
  bool passed = WaitFor([] { return logged_in.load(); });
  for (int round = 1; passed && round <= 3; ++round) {
    const int stereo_before = stereo_frames.load();
    const int grid_before = grid_frames.load();
    passed = RtcOpenRoom(room, VideoBroadcasting, false) == OK &&
             WaitFor([&] { return stereo_frames >= stereo_before + 5 && grid_frames >= grid_before + 5; });
    if (round == 2 && passed) {
      // 切到单路环视后关房，下一轮应恢复默认双目和环视网格。
      const auto command = vts_rtc::vision::EncodeViewControl(1, vts_rtc::vision::ViewMode::SurroundFront);
      passed = command && WaitFor([] { return view_peer.load() != 0; }) &&
               RtcSendData(view_peer, vts_rtc::vision::kVideoViewControlChannelLabel,
                           reinterpret_cast<const char*>(command.payload.data()), command.payload.size()) == OK &&
               WaitFor([] { return front_frames >= 5; });
    }
    passed = passed && RtcCloseRoom(room) == OK && WaitFor([&] { return closed_rooms == round; });
    std::cout << "round " << round << ": stereo=" << stereo_frames - stereo_before
              << " grid=" << grid_frames - grid_before << " front_total=" << front_frames
              << " closed_rooms=" << closed_rooms << std::endl;
  }
  RtcDestoryAgent();
  return passed ? 0 : 1;
}
