#include <c_rtc.h>

#include <atomic>
#include <chrono>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

namespace {
std::atomic<bool> logged_in{false};
std::atomic<bool> connected{false};
char kRoom[] = "sdk-loopback";
std::atomic<int> deleted_rooms{0};
std::atomic<bool> stale_peer{false};
std::atomic<int> message_round{0};
std::atomic<int> stereo_frames{0};
std::atomic<int> grid_frames{0};
}

int main(int argc, char** argv) {
  if (argc != 2) return 1;
  const bool sender = std::string(argv[1]) == "send";
  RtcInitParams options{};
  options.config_filepath = "rtc.cfg";
  options.serverconnection_state_handler = [](RtcServerConnectionState state) {
    logged_in = state == ServerLogined;
  };
  options.P2P_state_handler = [](RtcSessionId, RtcP2PState state) {
    connected = state == P2PConnected;
  };
  options.room_handler = [](RtcRoomOperation op, RtcRoomId room) {
    if (op == RoomDelete && room && std::string(room) == kRoom) {
      if (connected) stale_peer = true;
      ++deleted_rooms;
    }
  };
  // Both peers deliberately omit the optional data channel state callback.
  options.recv_msg_handler = [](RtcSessionId, const char* label, const char* data, size_t size) {
    if (std::string(label) == "smoke" && size == 1) message_round = data[0] - '0';
  };
  options.recv_frame_handler = [](RtcSessionId, const char* source, RtcMediaSourceType,
                                  size_t width, size_t height, size_t, const unsigned char* data, size_t size) {
    if (!source || !width || !height || !data || !size) return;
    if (std::string(source) == "stereo") ++stereo_frames;
    if (std::string(source) == "surround_grid") ++grid_frames;
  };

  if (RtcInitAgentV2(options) != OK) return 1;
  if (sender && (RtcAddExternalVideoSource("stereo", High) != OK ||
                 RtcAddExternalVideoSource("surround_grid", Low) != OK ||
                 RtcAddDataChannel("smoke", Medium, true, -1) != OK)) return 1;

  std::vector<unsigned char> pixels(640 * 360 * 3 / 2, 128);
  RtcYUV420pFrame frame{640, 360, 640, 320, 320, pixels.data(), pixels.size()};

  for (int round = 1; round <= 2; ++round) {
    const int first_stereo = stereo_frames.load();
    const int first_grid = grid_frames.load();
    bool joined = false;
    bool closing = false;
    for (int tick = 0; tick < 250 && deleted_rooms < round; ++tick) {
      if (!joined && logged_in && tick % 8 == 0) {
        joined = (sender ? RtcJoinRoom(kRoom) : RtcOpenRoom(kRoom, VideoBroadcasting, false)) == OK;
      }
      if (sender && joined && connected) {
        pixels.front() = static_cast<unsigned char>(tick);
        RtcSendFrame("stereo", &frame);
        RtcSendFrame("surround_grid", &frame);
        const char message = '0' + round;
        if (tick % 5 == 0) RtcBroadcastData("smoke", &message, 1);
      }
      if (!sender && !closing && message_round == round &&
          stereo_frames >= first_stereo + 15 && grid_frames >= first_grid + 15) {
        closing = RtcCloseRoom(kRoom) == OK;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(45));
    }
    if (stale_peer || deleted_rooms != round) break;
  }

  std::cout << argv[1] << " deleted_rooms=" << deleted_rooms
            << " stale_peer=" << stale_peer << " stereo_frames=" << stereo_frames
            << " grid_frames=" << grid_frames << " message_round=" << message_round << std::endl;
  const bool passed = deleted_rooms == 2 && !stale_peer &&
                      (sender || message_round == 2);
  RtcDestoryAgent();
  return passed ? 0 : 1;
}
