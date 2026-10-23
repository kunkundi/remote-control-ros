# dozer_bringup

启动编排与整车配置。

- `launch/dozer.launch.py`：总入口；`use_camera:=true` 启动真实相机驱动，`use_radar:=true` 启动雷达驱动，`use_mock_camera:=true` / `use_mock_radar:=true` 使用模拟数据
- `launch/radar.launch.py`：SR73F 雷达驱动（`replay_file:=` 可回放日志）
- `launch/static_tf.launch.py`：按 `config/extrinsics.yaml` 发布传感器外参
- `config/*.yaml`：各模块参数（radar、camera 等），统一在此维护
- `systemd/`：开机自启服务（后续添加）

## 当前实机联调

`fusion.yaml` 的 640x360 内参和 `extrinsics.yaml` 的安装位姿是**未经标定的联调初值**。
它们只用于验证节点启动、TF 查询和话题流转。当前实机配置使用 `video0/1`
和 SensorID 0 雷达。相机物理方向仍需核对；真实相机节点需要独占 V4L2 设备，
不能与 `rtc_edge` 同时采集。实车使用前须替换内外参。

使用模拟三雷达和三相机跑通话题链路：

```bash
source /opt/ros/humble/setup.bash
source install/setup.bash
ros2 launch dozer_bringup dozer.launch.py \
  use_mock_radar:=true use_mock_camera:=true use_perception:=true use_fusion:=true
```

另开终端查看 `/perception/fused_detections`。模拟相机发布纯色图，YOLO 通常
不会检测到目标，因此雷达目标会以 `class_id=4294967295`（unknown）输出；
这可验证三路节点与融合输出，但不能验证真实视觉匹配精度。使用真实相机时改为
`use_camera:=true`，关闭 `use_mock_camera`，并检查实际 `image_small` 分辨率是否为 640x360。

有桌面环境时，可在启动命令中加入 `use_viewer:=true`，弹出两路检测画面。
当前相机和算法已经运行时，只需另开终端执行
`ros2 run dozer_perception detection_viewer.py`，无需重复启动驱动。
