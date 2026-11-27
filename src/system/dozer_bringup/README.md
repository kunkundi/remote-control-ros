# dozer_bringup

启动编排与整车配置。

- `launch/dozer.launch.py`：总入口；默认 `image_backend:=shm` 从 edge 获取图像；`use_radar:=true` 启动雷达驱动；真实/模拟 ROS 相机仅在 `image_backend:=ros` 回退模式启用
- `launch/radar.launch.py`：SR73F 雷达驱动（`replay_file:=` 可回放日志）
- `launch/static_tf.launch.py`：按 `config/extrinsics.yaml` 发布传感器外参
- `config/*.yaml`：各模块参数（radar、camera 等），统一在此维护
- `systemd/`：开机自启服务（后续添加）

## 当前实机联调

`fusion.yaml` 的 640x360 内参和 `extrinsics.yaml` 的安装位姿是**未经标定的联调初值**。
它们只用于验证节点启动、TF 查询和话题流转。相机采集配置使用 `video0` 至
`video5`，依次对应 `front_left`、`front_right`、`front`、`rear`、`left`、
`right`；此映射的物理方向仍需核对。雷达配置使用 SensorID 0、1、2；预览脚本只启用 SensorID 0。
默认 edge 独占六路 V4L2 设备，每路均可经共享内存送到感知。RTC 设备映射见
`config/rtc_edge/edge.cfg`。整车脚本启用 `use_camera_topics:=true`，独立节点在某一路有订阅者时，将该路最新共享帧发布到 `/camera/<name>/image_small`。单独使用总 launch 时此开关默认关闭，适用于 `image_backend:=shm`。RTC edge 独占 V4L2 设备。
根脚本默认启用单进程六路 YOLO 批推理，`perception_cameras` 可选择任意非空子集；融合按配置处理三台雷达与六路相机，预览窗口只显示前左雷达。新增相机的
内外参须在标定后补充。实车使用前须替换现有联调内外参。

默认整套启动使用根目录 `./start_all.sh`；从旧 ROS 图像链路迁移需显式 `./start_all.sh --restart`。
六路 YOLO 批推理和三台雷达可用 `./start_all.sh` 启动，默认不打开窗口；也可给 launch 传入 `use_perception:=true perception_cameras:=front_left,front_right,front,rear,left,right`（需同时运行直采 edge）。六路模型并发性能尚需实机验收。
仅需直采 RTC 与音频时用 `./start_edge.sh`。保留模式拒绝旧 ROS 相机进程，不会自动抢占设备。

仅在调试回退模式启动六路 ROS 相机（不能同时运行直采 edge）：

```bash
source /opt/ros/humble/setup.bash
source install/setup.bash
ros2 launch dozer_bringup dozer.launch.py image_backend:=ros use_camera:=true
```

每路发布 `/camera/<camera_name>/image_small`，默认尺寸为 640x360，目标 30 FPS。
使用前确认六个设备均支持 1920x1080、至少 30 FPS 采集且未被其他进程占用。
修改 `config/camera.yaml` 后需要重启相机节点；默认 `--keep` 会保留旧进程及其参数，不会自动应用新帧率。

使用模拟三雷达和三相机跑通话题链路：

```bash
source /opt/ros/humble/setup.bash
source install/setup.bash
ros2 launch dozer_bringup dozer.launch.py \
  image_backend:=ros use_mock_radar:=true use_mock_camera:=true use_perception:=true use_fusion:=true
```

另开终端查看 `/perception/fused_detections`。模拟相机发布纯色图，YOLO 通常
不会检测到目标，因此雷达目标会以 `class_id=4294967295`（unknown）输出；
这可验证模拟数据发布、默认前双目检测节点与融合输出，但不能验证真实视觉匹配精度。使用真实相机时改为
`use_camera:=true`，关闭 `use_mock_camera`，并检查实际 `image_small` 分辨率是否为 640x360。

有桌面环境时，可在启动命令中加入 `use_viewer:=true`，弹出所选相机的检测画面。
当前相机和算法已经运行时，只需另开终端执行
`ros2 run dozer_perception detection_viewer.py`，无需重复启动驱动；查看 ROS 回退输入时增加 `--ros-args -p image_backend:=ros`。

在本机桌面终端可直接运行工作空间根目录的
`./start_detection_viewer.sh`。它检查图形会话后复用 `start_all.sh`，支持 `--keep`/`--restart`，
启动直采 edge、音频、`can0` 上的前左毫米波雷达、六路 YOLO、雷达视觉融合
与带框预览窗口，并弹出前左雷达坐标系的实时俯视图；按 Ctrl+C 停止本次新启动的模块。
首次使用 GPU 推理时，在工作空间根目录运行 `./setup_gpu_inference.sh` 建立
`.venv-gpu`；依赖仅安装在该环境中，启动脚本会先验证 CUDA 卷积。
默认 INT8 引擎随 `dozer_perception` 安装。启动前确保相机设备未被其他进程占用，`can0` 已配置且可用。
绿色检测框表示匹配到雷达目标，黄色框表示仅由 YOLO 检出。
当前内外参是联调初值，雷达与检测框的匹配结果须在标定后使用。
