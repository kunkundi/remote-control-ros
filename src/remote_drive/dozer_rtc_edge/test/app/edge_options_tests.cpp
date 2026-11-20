#include "edge_options.h"

#include <stddef.h>

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

#include <nlohmann/json.hpp>
#include <sys/stat.h>
#if defined(_WIN32)
#include <direct.h>
#else
#include <unistd.h>
#endif

namespace {

void Check(bool condition, const std::string& message) {
  if (!condition) {
    std::cerr << "FAILED: " << message << std::endl;
    std::exit(1);
  }
}

bool IsAsciiText(const std::string& text) {
  for (size_t i = 0; i < text.size(); ++i) {
    if (static_cast<unsigned char>(text[i]) > 127) {
      return false;
    }
  }
  return true;
}

nlohmann::json MakeValidConfig() {
  nlohmann::json root;
  root["edge"]["rtc"]["room"] = "test-room";
  root["edge"]["rtc"]["join_retry_ms"] = 1200;
  root["edge"]["rtc"]["status_interval_sec"] = 2;
  root["edge"]["rtc"]["frame_limit"] = 25;

  root["edge"]["audio"]["input_enabled"] = true;
  root["edge"]["audio"]["output_enabled"] = true;
  root["edge"]["audio"]["input_topic"] = "/test/audio";
  root["edge"]["audio"]["output_device"] = "USB speaker";

  root["edge"]["stereo_camera"]["left_device"] = "/dev/video4";
  root["edge"]["stereo_camera"]["right_device"] = "/dev/video5";

  root["edge"]["surround_camera"]["front_device"] = "/dev/video6";
  root["edge"]["surround_camera"]["rear_device"] = "/dev/video7";
  root["edge"]["surround_camera"]["left_device"] = "/dev/video8";
  root["edge"]["surround_camera"]["right_device"] = "/dev/video9";


  root["edge"]["vehicle_control"]["watchdog_ms"] = 250;
  root["edge"]["vehicle_control"]["state_interval_ms"] = 40;
  root["edge"]["vehicle_control"]["max_pending_events"] = 64;
  return root;
}

void WriteConfig(const std::string& path, const nlohmann::json& config) {
  std::ofstream output(path);
  Check(output.is_open(), "open temporary config");
  output << config.dump(2);
  Check(static_cast<bool>(output), "write temporary config");
}

void MakeDirectory(const std::string& path) {
#if defined(_WIN32)
  Check(::_mkdir(path.c_str()) == 0 || errno == EEXIST,
#else
  Check(::mkdir(path.c_str(), 0755) == 0 || errno == EEXIST,
#endif
        std::string("create temporary directory: ") + path);
}

void RemoveTestDirectory(const std::string& path) {
#if defined(_WIN32)
  ::_rmdir(path.c_str());
#else
  ::rmdir(path.c_str());
#endif
}

void TestValidConfig() {
  const std::string path = "rtc_edge_options_valid.json";
  nlohmann::json config = MakeValidConfig();
  config["edge"]["device_id"] = "car_1";
  WriteConfig(path, config);

  const rtc_edge_app::EdgeOptions options =
      rtc_edge_app::LoadEdgeOptions(path);
  Check(options.device_id == "car_1", "load device id");
  Check(!options.vehicle_debug_mode, "debug mode defaults off");
  config["edge"]["vehicle_control"]["debug_mode"] = true;
  WriteConfig(path, config);
  Check(rtc_edge_app::LoadEdgeOptions(path).vehicle_debug_mode, "load debug mode");
  config["edge"]["vehicle_control"]["debug_mode"] = "true";
  WriteConfig(path, config);
  bool debug_rejected = false;
  try {
    rtc_edge_app::LoadEdgeOptions(path);
  } catch (const std::runtime_error&) {
    debug_rejected = true;
  }
  Check(debug_rejected, "reject non-boolean debug mode");
  config["edge"]["vehicle_control"]["debug_mode"] = true;
  config["edge"]["dog_control"] = {
      {"enabled", true}, {"rosbridge_url", "ws://127.0.0.1:9090"},
      {"reconnect_interval_ms", 3000}, {"max_forward_speed", 1.0},
      {"max_angular_speed", 1.0}};
  WriteConfig(path, config);
  debug_rejected = false;
  try {
    rtc_edge_app::LoadEdgeOptions(path);
  } catch (const std::runtime_error&) {
    debug_rejected = true;
  }
  Check(debug_rejected, "reject debug and hardware control together");
  config["edge"].erase("dog_control");
  config["edge"]["vehicle_control"].erase("debug_mode");
  config["edge"].erase("device_id");
  WriteConfig(path, config);
  Check(rtc_edge_app::LoadEdgeOptions(path).device_id.empty(),
        "missing device id preserves legacy config");
  for (const auto& invalid : {nlohmann::json(""), nlohmann::json(123),
                             nlohmann::json("car 1"),
                             nlohmann::json("car\n1"),
                             nlohmann::json(std::string(65, 'a'))}) {
    config["edge"]["device_id"] = invalid;
    WriteConfig(path, config);
    bool rejected = false;
    try {
      rtc_edge_app::LoadEdgeOptions(path);
    } catch (const std::runtime_error& error) {
      rejected = std::string(error.what()).find("edge.device_id") !=
                 std::string::npos;
    }
    Check(rejected, "reject invalid device id");
  }
  Check(options.rtc.room_id == "test-room", "load room");
  Check(options.rtc.join_retry_ms == 1200, "load join retry");
  Check(options.frame_limit == 25, "load frame limit");
  Check(options.audio.input_enabled, "load audio input enabled");
  Check(options.audio.output_enabled, "load audio output enabled");
    Check(options.audio.input_topic == "/test/audio",
      "load audio input topic");
  Check(options.audio.output_device == "USB speaker",
        "load audio output device");
  Check(options.camera.left_device == "/dev/video4",
        "load left camera");
  Check(options.surround_camera.front_device == "/dev/video6",
        "load surround front camera");
  Check(options.surround_camera.right_device == "/dev/video9",
        "load surround right camera");
  Check(options.control.watchdog_ms == 250, "load watchdog");
  Check(options.control.max_pending_events == 64,
        "load pending event limit");
  std::remove(path.c_str());
}

void TestRepositoryConfig() {
  const rtc_edge_app::EdgeOptions options =
      rtc_edge_app::LoadEdgeOptions("rtc.cfg");
  Check(!options.rtc.room_id.empty(), "load repository room");
    Check(options.camera.left_device == "/dev/video0" &&
      options.camera.right_device == "/dev/video1",
      "load repository stereo devices");
    Check(options.surround_camera.front_device == "/dev/video2" &&
      options.surround_camera.rear_device == "/dev/video3" &&
      options.surround_camera.left_device == "/dev/video4" &&
      options.surround_camera.right_device == "/dev/video5",
      "load all surround camera devices");
  Check(options.control.watchdog_ms == 300,
        "load repository watchdog");
    Check(options.audio.input_enabled && options.audio.output_enabled,
      "repository audio matches upstream defaults");
  Check(options.audio.input_topic == "/audio/capture",
        "repository audio subscribes to capture node");
}

void TestMissingAudioSection() {
  const std::string path = "rtc_edge_options_audio_optional.json";
  nlohmann::json config = MakeValidConfig();
  config["edge"].erase("audio");
  WriteConfig(path, config);

  const rtc_edge_app::EdgeOptions options =
      rtc_edge_app::LoadEdgeOptions(path);
  Check(!options.audio.input_enabled,
        "missing audio section disables input");
  Check(!options.audio.output_enabled,
        "missing audio section disables output");
  Check(options.audio.input_topic == "/audio/capture",
        "missing audio section uses default input topic");
  std::remove(path.c_str());
}

void TestInvalidAudioTopic() {
  const std::string path = "rtc_edge_options_audio_topic.json";
  nlohmann::json config = MakeValidConfig();
  for (const auto& invalid : {nlohmann::json(""), nlohmann::json("default"),
                             nlohmann::json("/dev/snd/mic"), nlohmann::json(42)}) {
    config["edge"]["audio"]["input_topic"] = invalid;
    WriteConfig(path, config);
    bool rejected = false;
    try { rtc_edge_app::LoadEdgeOptions(path); }
    catch (const std::runtime_error& error) {
      rejected = std::string(error.what()).find("edge.audio.input_topic") != std::string::npos;
    }
    Check(rejected, "reject invalid audio input topic");
  }
  std::remove(path.c_str());
}

void TestLegacyAudioConfig() {
  const std::string path = "rtc_edge_options_audio_legacy.json";
  nlohmann::json config = MakeValidConfig();
  config["edge"]["audio"].erase("input_topic");
  config["edge"]["audio"]["input_device"] = "USB microphone";
  config["edge"]["audio"]["sample_rate"] = 48000;
  config["edge"]["audio"]["channels"] = 1;
  WriteConfig(path, config);
  const auto options = rtc_edge_app::LoadEdgeOptions(path);
  Check(options.audio.input_enabled && options.audio.input_topic == "/audio/capture",
        "legacy config defaults to ROS audio without opening a device");
  std::remove(path.c_str());
}

void TestRelativeMediaPath() {
  const std::string root = "rtc_edge_options_media_path_test";
  const std::string config_dir = root + "/config";
  const std::string media_dir = root + "/test_data";
  const std::string config_path = config_dir + "/rtc.cfg";
  const std::string media_path = media_dir + "/front.yuv";

  MakeDirectory(root);
  MakeDirectory(config_dir);
  MakeDirectory(media_dir);

  {
    std::ofstream media(media_path.c_str(), std::ios::binary);
    Check(media.is_open(), "open relative media file");
    media.put('\0');
    Check(static_cast<bool>(media), "write relative media file");
  }

  nlohmann::json config = MakeValidConfig();
  config["edge"]["surround_camera"]["front_device"] =
      "test_data/front.yuv";
  config["edge"]["surround_camera"]["rear_device"] = "";
  config["edge"]["surround_camera"]["left_device"] = "";
  config["edge"]["surround_camera"]["right_device"] = "";
  WriteConfig(config_path, config);

  const rtc_edge_app::EdgeOptions options =
      rtc_edge_app::LoadEdgeOptions(config_path);
  Check(options.surround_camera.front_device == media_path,
        "resolve media path under project root");

  std::remove(config_path.c_str());
  std::remove(media_path.c_str());
  RemoveTestDirectory(config_dir);
  RemoveTestDirectory(media_dir);
  RemoveTestDirectory(root);
}

void TestInvalidWatchdog() {
  const std::string path = "rtc_edge_options_watchdog.json";
  nlohmann::json config = MakeValidConfig();
  config["edge"]["vehicle_control"]["watchdog_ms"] = 301;
  WriteConfig(path, config);

  bool rejected = false;
  try {
    rtc_edge_app::LoadEdgeOptions(path);
  } catch (const std::runtime_error& ex) {
    const std::string error_message = ex.what();
    rejected = error_message.find("edge.vehicle_control.watchdog_ms") !=
                   std::string::npos &&
               IsAsciiText(error_message);
  }
  Check(rejected, "reject watchdog above safety limit");
  std::remove(path.c_str());
}

void TestMissingCameraDevice() {
  const std::string path = "rtc_edge_options_missing.json";
  nlohmann::json config = MakeValidConfig();
  config["edge"]["stereo_camera"].erase("right_device");
  WriteConfig(path, config);

  bool rejected = false;
  try {
    rtc_edge_app::LoadEdgeOptions(path);
  } catch (const std::runtime_error& ex) {
    const std::string error_message = ex.what();
    rejected = error_message.find("edge.stereo_camera.right_device") !=
                   std::string::npos &&
               IsAsciiText(error_message);
  }
  Check(rejected, "reject missing right camera device");
  std::remove(path.c_str());
}

void TestMissingSurroundCameraDevice() {
  const std::string path = "rtc_edge_options_surround_missing.json";
  nlohmann::json config = MakeValidConfig();
  config["edge"]["surround_camera"].erase("rear_device");
  WriteConfig(path, config);

  bool rejected = false;
  try {
    rtc_edge_app::LoadEdgeOptions(path);
  } catch (const std::runtime_error& ex) {
    const std::string error_message = ex.what();
    rejected = error_message.find("edge.surround_camera.rear_device") !=
                   std::string::npos &&
               IsAsciiText(error_message);
  }
  Check(rejected, "reject missing surround rear camera device");
  std::remove(path.c_str());
}

void TestEmptySurroundCameraDevice() {
  const std::string path = "rtc_edge_options_surround_empty.json";
  nlohmann::json config = MakeValidConfig();
  config["edge"]["surround_camera"]["front_device"] = "";
  WriteConfig(path, config);

  const rtc_edge_app::EdgeOptions options =
      rtc_edge_app::LoadEdgeOptions(path);
  Check(options.surround_camera.front_device.empty(),
        "allow empty surround camera device");
  Check(options.surround_camera.rear_device == "/dev/video7",
        "keep other surround camera enabled");
  std::remove(path.c_str());
}

void TestDogControlConfig() {
  const std::string path = "rtc_edge_options_dog.json";
  nlohmann::json config = MakeValidConfig();
  config["edge"]["dog_control"]["enabled"] = true;
  config["edge"]["dog_control"]["rosbridge_url"] =
      "ws://127.0.0.1:9090/bridge";
  config["edge"]["dog_control"]["reconnect_interval_ms"] = 500;
  config["edge"]["dog_control"]["max_forward_speed"] = 1.5;
  config["edge"]["dog_control"]["max_angular_speed"] = 2.0;
  WriteConfig(path, config);

  const rtc_edge_app::EdgeOptions options =
      rtc_edge_app::LoadEdgeOptions(path);
  Check(options.dog_control.enabled, "enable dog control");
  Check(options.dog_control.rosbridge_url ==
            "ws://127.0.0.1:9090/bridge",
        "load rosbridge URL");
  Check(options.dog_control.max_forward_speed == 1.5f,
        "load dog forward speed");
  Check(options.dog_control.max_angular_speed == 2.0f,
        "load dog angular speed");
  std::remove(path.c_str());
}

void TestInvalidDogSpeed() {
  const std::string path = "rtc_edge_options_dog_speed.json";
  nlohmann::json config = MakeValidConfig();
  config["edge"]["dog_control"]["enabled"] = true;
  config["edge"]["dog_control"]["rosbridge_url"] =
      "ws://127.0.0.1:9090";
  config["edge"]["dog_control"]["reconnect_interval_ms"] = 500;
  config["edge"]["dog_control"]["max_forward_speed"] = -1.0;
  config["edge"]["dog_control"]["max_angular_speed"] = 1.0;
  WriteConfig(path, config);

  bool rejected = false;
  try {
    rtc_edge_app::LoadEdgeOptions(path);
  } catch (const std::runtime_error& ex) {
    rejected = std::string(ex.what()).find(
                   "edge.dog_control.max_forward_speed") !=
               std::string::npos;
  }
  Check(rejected, "reject negative dog speed");
  std::remove(path.c_str());
}

}  // 匿名命名空间

int main(int argc, char** argv) {
  if (argc == 2 && std::string(argv[1]) == "--check-default-config") {
    const auto options = rtc_edge_app::ParseEdgeArgs(1, argv);
        Check(options.camera.left_device == "/dev/video0" &&
          options.camera.right_device == "/dev/video1",
          "default config resolves beside executable");
        Check(options.surround_camera.rear_device == "/dev/video3",
          "default config resolves referenced edge config");
    return 0;
  }
  {
    const std::string main_path = "rtc_edge_external_main.json";
    const std::string edge_path = "rtc_edge_external.json";
    auto root = MakeValidConfig();
    WriteConfig(edge_path, root.at("edge"));
    root["edge"] = {{"config", "rtc_edge_external.json"}};
    root["console"] = {{"config", "missing-console.json"}};
    WriteConfig(main_path, root);
    const auto options = rtc_edge_app::LoadEdgeOptions(main_path);
    Check(options.rtc.config_path == main_path,
          "external edge preserves main RTC config path");
    Check(options.rtc.room_id == MakeValidConfig()["edge"]["rtc"]["room"],
          "external edge loads runtime settings without loading console");
    std::remove(main_path.c_str());
    std::remove(edge_path.c_str());
  }
  TestValidConfig();
  TestRepositoryConfig();
  TestMissingAudioSection();
  TestInvalidAudioTopic();
  TestLegacyAudioConfig();
  TestRelativeMediaPath();
  TestInvalidWatchdog();
  TestMissingCameraDevice();
  TestMissingSurroundCameraDevice();
  TestEmptySurroundCameraDevice();
  TestDogControlConfig();
  TestInvalidDogSpeed();
  std::cout << "rtc_edge_options_tests passed" << std::endl;
  return 0;
}
