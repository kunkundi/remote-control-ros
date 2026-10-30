#include "dozer_rtc_edge/shared_camera_frame.h"

#include <sys/wait.h>
#include <array>
#include <iostream>
#include <memory>

void Require(bool condition, const char* message) {
  if (!condition) throw std::runtime_error(message);
}

void TestSixCameraIsolation() {
  const std::array<std::string, 6> cameras{
      "front_left", "front_right", "front", "rear", "left", "right"};
  const std::string prefix = "dozer_test_six_" + std::to_string(getpid()) + "_";
  std::vector<std::unique_ptr<rtc_edge::SharedCameraWriter>> writers;
  for (size_t index = 0; index < cameras.size(); ++index) {
    writers.push_back(std::make_unique<rtc_edge::SharedCameraWriter>(prefix + cameras[index], 24));
    std::vector<uint8_t> pixels(24, static_cast<uint8_t>(index + 1));
    Require(writers.back()->Publish(pixels.data(), pixels.size(), 4, 4, 100 + index,
                                   cameras[index]), "initial six-camera publish failed");
  }
  const int slow_reader = open(("/dev/shm/" + prefix + cameras.front()).c_str(), O_RDONLY);
  Require(slow_reader >= 0 && flock(slow_reader, LOCK_SH) == 0, "slow reader lock failed");
  for (size_t index = 0; index < cameras.size(); ++index) {
    std::vector<uint8_t> pixels(24, static_cast<uint8_t>(index + 17));
    const bool published = writers[index]->Publish(
        pixels.data(), pixels.size(), 4, 4, 200 + index, cameras[index]);
    Require(published == (index != 0), "one camera reader affected another camera");
    const int reader = open(("/dev/shm/" + prefix + cameras[index]).c_str(), O_RDONLY);
    rtc_edge::SharedCameraHeader header{};
    uint8_t payload[24]{};
    Require(reader >= 0 && flock(reader, LOCK_SH | LOCK_NB) == 0 &&
            pread(reader, &header, sizeof(header), 0) == sizeof(header) &&
            pread(reader, payload, sizeof(payload), sizeof(header)) == sizeof(payload),
            "six-camera snapshot read failed");
    close(reader);
    Require(std::string(header.frame_id) == cameras[index] &&
            header.sequence == (index == 0 ? 1u : 2u) &&
            header.timestamp_ns == (index == 0 ? 100 : 200 + index),
            "six-camera metadata mixed between streams");
    const uint8_t expected = static_cast<uint8_t>(index == 0 ? 1 : index + 17);
    for (const uint8_t value : payload) Require(value == expected, "six-camera pixels mixed");
  }
  close(slow_reader);
  writers.clear();
  for (const auto& camera : cameras) {
    const std::string path = "/dev/shm/" + prefix + camera;
    Require(access(path.c_str(), F_OK) != 0, "six-camera data file left behind");
    unlink((path + ".owner").c_str());
  }
}

int main(int argc, char** argv) {
  if (argc == 3 && std::string(argv[1]) == "--fixture") {
    rtc_edge::SharedCameraWriter writer(argv[2], 24);
    std::vector<uint8_t> pixels(24, 77);
    writer.Publish(pixels.data(), pixels.size(), 4, 4, 123456, "front_left");
    std::cout << "ready" << std::endl;
    std::string finish;
    std::getline(std::cin, finish);
    return 0;
  }
  const std::string name = "dozer_test_camera_" + std::to_string(getpid());
  const std::string path = "/dev/shm/" + name;
  try {
    TestSixCameraIsolation();
    {
      rtc_edge::SharedCameraWriter writer(name, 24);
      std::vector<uint8_t> pixels(24, 77);
      Require(writer.Publish(pixels.data(), pixels.size(), 4, 4, 123456, "front_left"), "publish failed");
      bool rejected = false;
      try { rtc_edge::SharedCameraWriter duplicate(name, 24); }
      catch (const std::runtime_error&) { rejected = true; }
      Require(rejected, "duplicate producer accepted");
      const pid_t child = fork();
      Require(child >= 0, "fork failed");
      if (child == 0) {
        const int descriptor = open(path.c_str(), O_RDONLY);
        rtc_edge::SharedCameraHeader header{};
        uint8_t payload[24]{};
        const bool valid = descriptor >= 0 && flock(descriptor, LOCK_SH) == 0 &&
            pread(descriptor, &header, sizeof(header), 0) == sizeof(header) &&
            pread(descriptor, payload, sizeof(payload), sizeof(header)) == sizeof(payload) &&
            header.sequence == 1 && header.timestamp_ns == 123456 &&
            header.width == 4 && header.height == 4 &&
            std::memcmp(header.magic, "DZRI420", 8) == 0 &&
            std::memcmp(payload, pixels.data(), sizeof(payload)) == 0;
        if (descriptor >= 0) close(descriptor);
        _exit(valid ? 0 : 1);
      }
      int status = 0;
      Require(waitpid(child, &status, 0) == child && WIFEXITED(status) && WEXITSTATUS(status) == 0,
              "cross-process frame mismatch");
      const int reader = open(path.c_str(), O_RDONLY);
      Require(reader >= 0 && flock(reader, LOCK_SH) == 0, "reader lock failed");
      Require(!writer.Publish(pixels.data(), pixels.size(), 4, 4, 123457, "front_left"),
              "producer blocked or overwrote locked snapshot");
      close(reader);
      Require(writer.Publish(pixels.data(), pixels.size(), 4, 4, 123458, "front_left"), "resume failed");
      rejected = false;
      try { writer.Publish(pixels.data(), 23, 4, 4, 1, "front_left"); }
      catch (const std::invalid_argument&) { rejected = true; }
      Require(rejected, "truncated frame accepted");
    }
    Require(access(path.c_str(), F_OK) != 0, "producer left stale data file");
    unlink((path + ".owner").c_str());
    std::cout << "Shared camera cross-process and backpressure tests passed\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}