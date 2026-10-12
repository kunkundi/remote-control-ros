# dozer_camera_gmsl

GMSL 相机驱动（驱动层），遵守工作空间 README 第 5 节驱动层规范。负责人：待定（建议由 RTC 模块负责人兼任，代码来自 rtc-framework）。

## 硬件

- 载板 8 路 GMSL，`/dev/video0-7`（tegra-video），UYVY 4:2:2
- 分辨率 3840x2160 / 1920x1536，30/50/60 fps
- 当前使用：双目 video0/1（拼接为 `merged_image`）+ 环视前后左右 video2-5（`surround_*`），映射见 `dozer_bringup/config/camera.yaml`

## 职责

- V4L2 采集、CUDA 格式转换（UYVY → I420 / 双目拼接）、采集时间戳、帧序号
- 不包含编码推流（属于 `dozer_rtc_bridge`），不包含算法（属于感知层）

## 分阶段实施

| 阶段 | 形式 | 说明 |
|---|---|---|
| 第一阶段 | C++ 共享库 | 从 rtc-framework `client/modules/camera/`（`v4l2_camera`、`single/`、`dual/`、CUDA 转换）迁移；`dozer_rtc_bridge` 直接链接，行为与现有 `rtc_edge` 一致 |
| 第二阶段 | ROS 2 component | 与 `dozer_rtc_bridge` 加载到同一个 component container；帧类型用 `rclcpp::TypeAdapter`（REP-2007）适配 `sensor_msgs/Image`：同进程订阅者拿到共享 CUDA/DMA 缓冲区（零拷贝），只有 rosbag/rviz 等跨进程订阅者才转换为 Image 消息 |

## 接口（第二阶段）

| Topic | 类型 | 说明 |
|---|---|---|
| `/camera/<name>/image_raw` | `sensor_msgs/Image`（进程内为适配类型） | 全分辨率帧，跨进程订阅代价大，仅调试/录制用 |
| `/camera/<name>/image_small` | `sensor_msgs/Image` | 降采样帧（≤ 15 Hz），供感知算法跨进程订阅 |
| `/diagnostics` | `diagnostic_msgs/DiagnosticArray` | 帧率、丢帧、采集错误 |

`header.stamp` 为帧采集时刻，`header.frame_id` 为 `camera_<name>`。
