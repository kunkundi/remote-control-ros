# dozer_rtc_edge

独立边缘端应用，源码从 `rtc-framework/client` 移植，使用本工作空间的 colcon/ament_cmake 编译，不调用 xmake。位于 `src/remote_drive/`，不属于纯通信层。上游文件集中在 `upstream/client/`；ROS 输入适配位于本包 `src/`。

## 相机复用

由 `dozer_camera_gmsl` 独占相机设备，edge 只订阅 ROS `sensor_msgs/Image`，不打开 `/dev/video*`。当前支持偶数尺寸的 `bgr8`/`rgb8` 和带行填充的图像。输入 QoS 为 SensorData、KeepLast(1)，只缓存最新帧。双目尺寸必须一致，时间戳差不得超过 30 ms，旧帧丢弃后等待新帧；左右图横向拼接并转成 I420 后发送。

默认订阅双目 `/camera/front_left/image_small`、`/camera/front_right/image_small`，以及四路环视 `/camera/front/image_small`、`/camera/rear/image_small`、`/camera/left/image_small`、`/camera/right/image_small`。现有相机配置仅发布双目，每路为 640x360、5 Hz，输出双目为 1280x360；四路环视需要对应 ROS 图像发布节点上线。此路径经 DDS 和 CPU 图像转换，不是全分辨率零拷贝；不得据此声称达到原直接采集版本的性能。

`left_device`、`right_device` 及环视 `*_device` 字段填写绝对 ROS topic，不接受设备路径或本地 YUV 文件。空环视字段禁用该路。相机配置仅保留 topic 字段，已移除无效的宽高、缓冲区、超时、预热和帧等待参数及其解析逻辑。实际尺寸来自 ROS 图像消息；采集尺寸和发布频率由相机驱动配置控制。

## 构建

```sh
source /opt/ros/humble/setup.bash
colcon build --packages-select dozer_rtc_edge --cmake-args -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON
colcon test --packages-select dozer_rtc_edge --event-handlers console_direct+
colcon test-result --verbose
```

需要 SDL3、nanopb 0.4.9、libyuv、OpenCV、nlohmann_json、Asio、OpenSSL 和外部 vtsrtc C ABI SDK。默认 SDK 位于 `~/workspace/rtc-framework/install`，可通过 `RTC_FRAMEWORK_INSTALL` 覆盖；可选 rosbridge 转发器还使用 `SIMPLE_WS_INCLUDE_DIR` 指定的 Simple-WebSocket-Server 头文件。系统依赖优先按 CMake 搜索规则查找；本机已安装的 xmake 包缓存仅作为搜索提示，可用 `RTC_DEPENDENCY_CACHE` 覆盖，也可通过 `CMAKE_PREFIX_PATH` 指定其他安装位置。第三方库和 SDK 不复制进仓库。

## 运行

先启动现有 ROS 相机 launch。确认 `dozer_bringup/config/rtc_edge/rtc.cfg` 中与上游对齐的信令地址及 ICE 配置适用于当前部署。随后在工作空间根目录执行：

```sh
source install/setup.bash
ros2 run dozer_rtc_edge rtc_edge --config "$PWD/src/system/dozer_bringup/config/rtc_edge/rtc.cfg"
```

也可以直接执行 `ros2 run dozer_rtc_edge rtc_edge`，默认从可执行文件附近读取安装的配置。构建时会将集中配置复制到构建和安装目录，兼容普通安装与 `--symlink-install`；修改源配置后需重新构建 `dozer_rtc_edge`，或使用上述 `--config` 直接指定源配置。

配置集中在 dozer_bringup，不自动启动第二套相机。RTC 服务、ICE、编码和音频设置与本机 rtc-framework 配置对齐；保留 ROS 相机话题、输入尺寸和车辆调试模式，不引入控制台配置。配置包含 TURN 认证信息，不应公开分发。配置订阅双目和四路环视。所有相机输入共享进程默认 ROS context；停止单路输入不关闭共享 context。每路输入仍有独立节点和执行器。相机话题仍建议在配置中填写；ROS 命令行参数由 rclcpp 统一处理。

## 日志

应用日志通过名为 `rtc_edge` 的 ROS 节点输出，统一进入终端、ROS 日志文件和 `/rosout`，不再生成独立的 `rtc_edge.log`。文件默认位于 `~/.ros/log`，遵循 `ROS_LOG_DIR` 和 `ROS_HOME`。可用 `ros2 topic echo /rosout` 查看聚合日志，用 `ros2 run dozer_rtc_edge rtc_edge --ros-args --log-level rtc_edge:=error` 设置日志级别，也可通过标准 ROS 参数禁用对应输出。

底层 RTC SDK 日志也使用 ROS 实际日志目录：优先 `ROS_LOG_DIR`，其次 `$ROS_HOME/log`，默认 `~/.ros/log`；使用 ROS launch 时跟随其设置的日志目录。用户配置不包含日志路径字段，应用选项和日志初始化接口也不接收路径。仅为兼容 SDK 配置格式，适配层在临时配置中注入 `log_path`，源配置不被改写。临时配置权限为 `0600`，在 SDK 初始化结束后删除。`rtc_agent.log` 仍由 SDK 自行管理，与应用日志同目录但不合并文件，也不发布到 `/rosout`。应用日志使用 ROS 后端，旧 spdlog 后端及其构建依赖已移除。

## 范围与限制

保留会话、视频源、音频、视图切换、控制协议和 300 ms 看门狗。车辆控制默认 debug 模式，不会替代 `dozer_vehicle`，也尚未接入 `/vehicle/*`。禁止在未实现并审查车辆适配前用于真实车辆执行。

相机相关本地 YOLO 路径已从构建移除；启用 edge YOLO 会明确报错。检测由现有 `dozer_perception` 负责，但 ROS 检测结果到 RTC DataChannel 的翻译尚未实现。`dozer_rtc_bridge` 骨架保持不变，避免混淆模块职责。

测试包含上游参数、视频发送规划、控制协议、控制模块及视图协议回归，以及 ROS 图像校验、发布订阅和双目配对。构建和无硬件测试不代表已完成实机相机、远程视频或控制链路验证。