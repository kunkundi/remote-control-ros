# dozer_rtc_edge

独立远程驾驶边缘端应用，源码从 `rtc-framework/client` 移植，使用本工作空间的 colcon/ament_cmake 编译，不调用 xmake。位于 `src/remote_drive/`，不属于纯通信层。移植代码按本包职责组织，不再保留上游的应用/模块目录套层。

## 目录结构

```text
dozer_rtc_edge/
|-- CMakeLists.txt
|-- package.xml
|-- README.md
|-- include/
|   |-- dozer_rtc_edge/    # ROS 图像输入与 SDK 配置适配
|   `-- rtc_*/            # 移植模块头文件，保留原有包含路径
|-- src/
|   |-- app/              # 主入口、应用编排、参数、视频发送规划
|   |-- ros/              # ROS 图像、音频输入与日志实现
|   |-- runtime/          # 进程生命周期与 RTC 会话
|   |-- vehicle/          # 车辆控制接口与安全看门狗
|   |-- dog/              # 可选指令转发
|   |-- protocols/        # 控制与视觉协议编解码
|   `-- vision/           # 保留的本地视觉实现，当前不参与构建
|-- protocols/
|   |-- vehicle/          # schema/ 定义与 generated/ nanopb 代码
|   `-- vision/           # schema/ 定义与 generated/ nanopb 代码
|-- test/                 # 按 app、ros、vehicle、protocols 等职责组织
`-- docs/upstream/        # 移植来源的参考说明，不作为当前构建入口
```

应用私有头文件与入口放在 `src/app/`，跨模块头文件统一放在 `include/`。
保留 `rtc_runtime/...` 等包含路径和命名空间，避免目录调整波及业务接口。
协议定义与生成文件成组保存，不手工修改 nanopb 生成代码；编解码实现和测试分别
位于 `src/protocols/` 与 `test/protocols/`。原先未启用的移植源码和测试仍保留，
实际构建与执行范围以 `CMakeLists.txt` 为准。
运行配置继续集中在 `dozer_bringup/config/rtc_edge/`，不在本包复制维护另一份源配置。

音频设备枚举、PCM 采集与播放由 `src/drivers/dozer_audio/` 独立驱动包提供。
采集由独立 `audio_capture_node` 负责，本包只订阅 PCM 话题、发送 RTC 音频和编排远端播放，不再打开录音设备、直接编译音频设备实现或查找 SDL3。
原有 `rtc_audio/audio_device.h` 包含路径与接口保持不变，由驱动包导出的 CMake target 提供。
构建本包及工作空间内依赖时使用 `colcon build --packages-up-to dozer_rtc_edge`。

## 相机直采与共享内存

默认由 edge 独占 `/dev/video0` 至 `/dev/video5`，依次为前左、前右、前、后、左、右；这是沿用的配置映射，物理方向仍需核对。不要同时启动 `dozer_camera_gmsl` 或其他采集程序。复用仓库内 `vendor/rtc-framework` 的 V4L2 异步采集和单目 CUDA 转换实现，接受 YUYV/UYVY/NV12，采集请求为 1920x1080，转换为 I420 后用 libyuv 缩至每路 640x360。目标为设备的 30 FPS，不补帧，不承诺处理或 console 接收达到 30 FPS。

采集/转换线程与 RTC 发送线程分离：没有 console、没有选择视图时仍持续采集。双目直接拼接 I420 平面为 1280x360，配对容差 30 ms，旧帧丢弃；环视网格为 640x360。默认视频热路径没有 ROS 图像序列化，也没有 BGR 往返转换；YOLO/预览按需在消费端把 I420 转成 BGR。仍有转换、缩放与共享内存快照拷贝，不是 DMA-BUF 或端到端零拷贝。

六路各发布一个独立的最新帧快照：`/dev/shm/dozer_camera_<name>`，名称为 front_left、front_right、front、rear、left、right。RTC 环视通道的 `surround_` 前缀不进入共享文件名或相机标识。一路读者持锁不会阻塞其他路。文件权限 0600，消费者与 edge 需同一用户。协议为小端、88 字节头加紧密 I420 负载（640x360 时 345600 字节）；包含版本、帧序号、系统时间纳秒、单调发布时间、尺寸、负载长度和相机标识。时间戳由 V4L2 出队的单调时刻映射到系统时钟，不是硬件曝光时间；检测结果原样保留该时间戳与共享帧序号。

生产者持独占 owner 锁，发布/读取快照用非阻塞文件锁，冲突时跳帧而不阻塞采集。消费者拷贝后立刻释放锁，再转换与推理；60 Hz 轮询不代表 60 FPS，同一序号只消费一次。生产者拒绝处理后超过 200 ms 的输入，消费者拒绝发布后超过 200 ms 的快照。重启以新 inode 原子替换文件，消费者自动跟随；正常退出删除数据文件，保留 `.owner` 锁文件。异常退出留下的数据会因过期被拒绝，不要在 producer 运行时删除 `.owner`。

保留显式 ROS 回退：把 `left_device`、`right_device` 和环视 `*_device` 改成绝对图像 topic，启动感知时指定 `image_backend:=ros`；双目不能混用设备与 topic。ROS 输入支持偶数尺寸 `bgr8`/`rgb8`、行填充和 KeepLast(1)，不发布共享内存。空环视字段禁用该路。本地 YUV 文件不受支持。默认直采路径的分辨率固定在 `DirectImageSource`，`camera.yaml` 只影响 ROS 驱动回退路径。

## 构建

```sh
source /opt/ros/humble/setup.bash
colcon build --packages-up-to dozer_rtc_edge dozer_bringup --cmake-args -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON
colcon test --packages-select dozer_audio dozer_rtc_edge --event-handlers console_direct+
colcon test-result --verbose
```

本包使用工作空间的 `dozer_rtc_sdk` 编译和安装 vtsrtc，通过 `dozer_media_vendor` 取得 Asio、JSON、nanopb 和 libyuv。OpenCV、OpenSSL 与 CUDA Toolkit 由系统提供。

## 运行

直采使用仓库内 `vendor/rtc-framework/client/modules/camera/src` 的 V4L2 和 CUDA 实现。默认 CUDA 架构为 Orin 的 87，可用 `-DCMAKE_CUDA_ARCHITECTURES=...` 覆盖；找不到 nvcc 时指定 `-DCMAKE_CUDA_COMPILER=/usr/local/cuda/bin/nvcc`。

先确认没有其他进程占用相机。启用音频输入时，另一个终端启动 `ros2 launch dozer_bringup audio.launch.py`，或使用根目录 `./start_edge.sh`。确认集中配置的信令地址及 ICE 配置适用于当前部署。单独启动 edge：

```sh
source install/setup.bash
ros2 run dozer_rtc_edge rtc_edge --config "$PWD/src/system/dozer_bringup/config/rtc_edge/rtc.cfg"
```

也可以直接执行 `ros2 run dozer_rtc_edge rtc_edge`，默认从可执行文件附近读取安装的配置。构建时会将集中配置复制到构建和安装目录，兼容普通安装与 `--symlink-install`；修改源配置后需重新构建 `dozer_rtc_edge`，或使用上述 `--config` 直接指定源配置。

配置集中在 dozer_bringup，车辆 debug 模式和 300 ms 看门狗保持不变。配置包含 TURN 认证信息，不应公开分发。直采图像不经 ROS；音频及 ROS 图像回退输入共享默认 ROS context，停止单路输入不关闭 context。ROS 命令行参数由 rclcpp 统一处理。从旧版切换时必须显式执行 `./start_all.sh --restart`（整套）或 `./start_edge.sh --restart`（只启用 edge 和音频）；默认 `--keep` 会拒绝保留旧 ROS 相机进程。

## 音频订阅

`edge.audio.input_enabled` 控制是否发送采集音频，`input_topic` 默认 `/audio/capture`，
消息类型为 `dozer_msgs/msg/AudioFrame`。输入 QoS 为 SensorData、KeepLast(2)，收到完整
10 ms、16 位有符号 PCM 后同步调用 RTC 发送接口；错误采样率、声道、帧数或字节数会丢弃并限频告警。
采样率支持 8000、16000、32000、44100、48000 Hz，声道支持 1 或 2；格式来自消息。
设备名、采样率、声道配置已移到 `dozer_bringup/config/audio.yaml`。
旧配置的 `input_device`、`sample_rate`、`channels` 不再生效；没有 `input_topic` 时使用默认话题，
不会退回直接录音。没有采集发布者时不发送输入音频，节点可晚于 edge 启动。
`output_enabled` 和 `output_device` 仍控制 edge 进程内的远端音频播放。

## 日志

应用日志通过名为 `rtc_edge` 的 ROS 节点输出，统一进入终端、ROS 日志文件和 `/rosout`，不再生成独立的 `rtc_edge.log`。文件默认位于 `~/.ros/log`，遵循 `ROS_LOG_DIR` 和 `ROS_HOME`。可用 `ros2 topic echo /rosout` 查看聚合日志，用 `ros2 run dozer_rtc_edge rtc_edge --ros-args --log-level rtc_edge:=error` 设置日志级别，也可通过标准 ROS 参数禁用对应输出。

底层 RTC SDK 日志也使用 ROS 实际日志目录：优先 `ROS_LOG_DIR`，其次 `$ROS_HOME/log`，默认 `~/.ros/log`；使用 ROS launch 时跟随其设置的日志目录。用户配置不包含日志路径字段，应用选项和日志初始化接口也不接收路径。仅为兼容 SDK 配置格式，适配层在临时配置中注入 `log_path`，源配置不被改写。临时配置权限为 `0600`，在 SDK 初始化结束后删除。`rtc_agent.log` 仍由 SDK 自行管理，与应用日志同目录但不合并文件，也不发布到 `/rosout`。应用日志使用 ROS 后端，旧 spdlog 后端及其构建依赖已移除。

## 范围与限制

保留会话、视频源、音频、视图切换、控制协议和 300 ms 看门狗。车辆控制默认 debug 模式，不会替代 `dozer_vehicle`，也尚未接入 `/vehicle/*`。禁止在未实现并审查车辆适配前用于真实车辆执行。

本地 YOLO 已从 edge 和 console 移除。`dozer_perception` 仍负责视觉检测，`dozer_fusion` 在 `/perception/fused_detections` 发布 `dozer_msgs/msg/FusedDetectionArray`。edge 按 `source_id` 保留每路最新待发快照，在打开的 `perception.fused.v1` DataChannel 上按设备发送；通道无序、不重传，单包上限为 16 KiB，超限丢弃整包，不影响车辆控制协议。

两端构建均需要 `libprotobuf-dev` 和 `protobuf-compiler`，通过同一个 `src/remote_drive/protocols/fused_detection.proto` 生成 C++ 类型。该通道使用独立序号丢弃乱序包，同时保留 ROS 原始时间戳、`frame_seq`、坐标系和全部目标字段。升级时需同步部署 edge 和 console；车辆控制仍使用原 nanopb 协议。

现场启动后，可先用 `ros2 topic echo /perception/fused_detections --once` 确认 ROS 侧已有数据，再让两端进入同一 RTC 房间，观察对应相机叠框。仅启动 edge 不会启动感知节点或生成检测数据。

测试包含上游参数、视频发送规划、控制协议、控制模块及视图协议回归，以及 ROS 图像校验、发布订阅和双目配对。音频订阅测试覆盖 PCM 格式、坏帧拒绝、音频与相机共享 context；`dozer_audio` 使用 SDL dummy 后端覆盖设备层和采集节点发布，不打开真实声卡。构建和无硬件测试不代表已完成实机相机、音频、远程视频或控制链路验证。

直采新增 I420 拼接、共享内存跨进程与 Python 互通测试。可用 `build/dozer_rtc_edge/direct_image_source_tests --cuda` 验证真实 CUDA 内核的合成 YUYV/UYVY/NV12 像素，不打开摄像头。Python 互通测试设置 `DOZER_SHM_TEST_WRITER` 为 `build/dozer_rtc_edge/shared_camera_frame_tests` 的绝对路径。

edge 崩溃、SDK 初始化失败、音频初始化失败或相机致命错误会终止整个 edge，感知因此失去新图像；当前方案不是独立采集守护进程。普通网络断连、未入房或未选视图不会关闭采集。慢消费者只取得最新帧，不保证每帧推理；环视现有四路更新屏障仍保留。实机六路吞吐、编码帧率、console 接收帧率和重连恢复仍需现场验收。