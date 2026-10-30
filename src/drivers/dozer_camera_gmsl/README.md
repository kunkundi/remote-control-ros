# dozer_camera_gmsl

独立的 ROS 2 V4L2 相机采集节点。参考 `rtc-framework/client/modules/camera/`
的格式协商、mmap 缓冲、取帧与回收流程实现；不启动 RTC、编码、推流或视觉算法。
每个相机运行一个 `camera_gmsl_node` 进程。

- 输入：配置中的 `/dev/videoN`，支持 YUYV 和 UYVY 4:2:2，V4L2 mmap streaming。
- 输出：`/camera/<camera_name>/image_small`（`sensor_msgs/Image`，`bgr8`，SensorData QoS）。
- `header.frame_id` 为 `camera_<camera_name>`；`header.stamp` 为出队后采集节点的 ROS 时间。
- `small_image_downscale` 做最近邻降采样；`small_image_max_fps` 控制目标发布频率，默认 30 FPS。限帧使用固定时间相位并容忍半个周期的到帧抖动，不从转换完成后重新计时；慢输入不补帧，长时间停顿后不突发补发。
- V4L2 错误帧会丢弃并限频告警，不发布损坏图像。

参数在 `dozer_bringup/config/camera.yaml`。总启动入口可用
`use_camera:=true` 启动当前的 `/dev/video0/1` 两路。
两路的前左、前右名称仅供联调，物理方向尚未核对；同一设备不能与
`rtc_edge` 同时采集。

仅测试一台设备时，可直接覆写参数，例如：

```bash
ros2 run dozer_camera_gmsl camera_gmsl_node --ros-args \
  -r __node:=camera_front_left \
  -p camera_name:=front_left -p device:=/dev/video0 \
  -p width:=1920 -p height:=1080 \
  -p small_image_downscale:=3 -p small_image_max_fps:=30.0
```

上述示例发布 640x360 图像，但必须先确认设备可用且没有被其他进程占用。
30 FPS 是软件发布目标，要求设备本身提供至少 30 FPS，且采集、转换和 DDS 处理能力足够；驱动不会用重复帧补足帧率。
