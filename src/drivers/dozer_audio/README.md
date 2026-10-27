# dozer_audio

Reusable SDL3 audio device driver, built with colcon/ament_cmake. The driver
provides a ROS PCM capture node and a reusable device enumeration, capture and
playback library. It has no RTC sessions or RTC SDK dependencies. The RTC
application subscribes to captured audio and owns RTC transport and playback.

## Layout

```text
dozer_audio/
|-- cmake/sdl3.cmake
|-- include/rtc_audio/audio_device.h
|-- src/audio_device.cpp
|-- src/audio_capture_node.h
|-- src/audio_capture_node.cpp
|-- src/main.cpp
|-- test/audio_device_tests.cpp
|-- test/audio_capture_node_tests.cpp
`-- docs/upstream.md
```

The existing `rtc_audio` namespace and include path remain unchanged. Capture
frames are signed 16-bit PCM; a frame's buffer is valid only during its callback.
Capture and playback share device discovery and SDL audio subsystem management.

## Build And Test

SDL3 must provide its CMake package. A system installation or `SDL3_DIR` can be
used; the existing `$HOME/.xmake/packages` cache is also searched for SDL3.
Override that cache with `DOZER_AUDIO_DEPENDENCY_CACHE` when necessary. No xmake
command is run.

```bash
source /opt/ros/humble/setup.bash
colcon build --packages-up-to dozer_audio
colcon test --packages-select dozer_audio --event-handlers console_direct+
colcon test-result --test-result-base build/dozer_audio --verbose
```

Tests use SDL's dummy audio backend and do not open physical audio devices.

## Capture Node

Build `dozer_bringup`, source the workspace, then start capture independently:

```bash
colcon build --packages-up-to dozer_bringup
source install/setup.bash
ros2 launch dozer_bringup audio.launch.py
```

The main bringup also accepts `use_audio:=true` (disabled by default).
Parameters live in `dozer_bringup/config/audio.yaml`: `device_name`,
`sample_rate`, `channels`, `frame_id`, and `topic`. An alternate file can be
selected with `params_file:=/absolute/path/audio.yaml`. Restart capture after
changing parameters. Direct execution is also supported:

```bash
ros2 run dozer_audio audio_capture_node --ros-args --params-file src/system/dozer_bringup/config/audio.yaml
```

The default output is `/audio/capture` (`dozer_msgs/msg/AudioFrame`), using
SensorData QoS with KeepLast(2). Each frame contains 10 ms of interleaved signed
16-bit PCM in native byte order (little-endian on the deployed Jetson). Supported
sample rates are 8000, 16000, 32000, 44100 and 48000 Hz, with one or two channels.
Defaults are 48000 Hz and mono. `frame_count` is the number of samples per
channel; payload size is `frame_count * channels * 2` bytes. The header timestamp
is the ROS time when the completed frame is published, not a hardware timestamp.
Frame payloads are copied before the capture callback returns.

The edge application uses `audio.input_topic` instead of opening a recording
device. Audio output remains an in-process playback library, not a ROS node.
These tests do not establish microphone quality or end-to-end RTC performance.

## Use From Another Package

Declare a dependency on `dozer_audio` in the consuming package manifest, then:

```cmake
find_package(dozer_audio REQUIRED)
target_link_libraries(your_target PRIVATE dozer_audio::dozer_audio)
```