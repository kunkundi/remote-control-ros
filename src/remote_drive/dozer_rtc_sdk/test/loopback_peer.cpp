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
std::atomic<bool> received_message{false};
std::atomic<int> received_frames{0};
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
  // Both peers deliberately omit the optional data channel state callback.
  options.recv_msg_handler = [](RtcSessionId, const char* label, const char* data, size_t size) {
    if (std::string(label) == "smoke" && std::string(data, size) == "loopback")
      received_message = true;
  };
  options.recv_frame_handler = [](RtcSessionId, const char* source, RtcMediaSourceType,
                                  size_t width, size_t height, size_t, const unsigned char* data, size_t size) {
    if (source && width && height && data && size) ++received_frames;
  };

  if (RtcInitAgentV2(options) != OK) return 1;
  if (sender && (RtcAddExternalVideoSource("stereo", High) != OK ||
                 RtcAddDataChannel("smoke", Medium, true, -1) != OK)) return 1;

  char room[] = "sdk-loopback";
  std::vector<unsigned char> pixels(640 * 360 * 3 / 2, 128);
  RtcYUV420pFrame frame{640, 360, 640, 320, 320, pixels.data(), pixels.size()};
  bool joined = false;
  bool sent = false;
  for (int tick = 0; tick < 500; ++tick) {
    if (!joined && logged_in && tick % 8 == 0) {
      joined = (sender ? RtcJoinRoom(room) : RtcOpenRoom(room, VideoBroadcasting, false)) == OK;
    }
    if (sender && joined && connected) {
      pixels.front() = static_cast<unsigned char>(tick);
      sent |= RtcSendFrame("stereo", &frame) == OK;
      if (tick % 5 == 0) RtcBroadcastData("smoke", "loopback", 8);
    }
    if ((sender && sent && !connected) || (!sender && received_frames >= 30 && received_message)) break;
    std::this_thread::sleep_for(std::chrono::milliseconds(45));
  }

  std::cout << argv[1] << " joined=" << joined << " received_frames=" << received_frames.load()
            << " received_message=" << received_message.load() << std::endl;
  RtcDestoryAgent();
  return (sender ? sent : received_frames >= 30 && received_message) ? 0 : 1;
}
