# dozer_perception

感知算法节点（感知层），只通过 topic 获取传感器数据，不链接驱动代码。负责人：待定。

- 输入：当前两路 `/camera/<name>/image_small`（sensor_msgs/Image）
- 输出：前左 `/perception/detections`，前右 `/perception/front_right/detections`（Detection2DArray，header.stamp 等于输入帧时间戳）
- 实现：`yolo_node.py` 使用 Ultralytics YOLO；在 ROS Python 环境中安装
  `ultralytics` 及对应平台推理依赖。车端默认 `device=0`，要求 CUDA 可用；
  图像订阅只保留最新 1 帧，避免慢推理时积压旧画面。
- 支持 `rgb8`、`bgr8`、`rgba8`、`bgra8`，输出时间戳沿用输入图像采集时间。
- 模型文件放在本包的 `model/` 目录，默认文件名为 `best.pt`；
  `dozer_bringup/config/perception.yaml` 已将相机节点指向该文件。
  权重文件不入 Git，需自行放入实际模型。

`detection_viewer.py` 将两路检测框叠加到对应时间戳的图像上，并在匹配到雷达目标时显示其 3D 坐标。绿色框表示已融合，黄色框表示仅 YOLO 检测。画面顶部显示 YOLO 数量及未分类雷达目标数量。它发布 `/perception/front_left/annotated_image` 和 `/perception/front_right/annotated_image`；有图形桌面时显示相机合并窗口及独立的前左雷达俯视图窗口。雷达图直接使用 `/radar/front_left/objects` 的坐标：前方朝上、左侧朝左，显示前方 0–6 m、左右各 3 m，网格间隔 1 m；点旁数字为雷达目标 ID，绿色点表示相对速度接近零，橙色点表示运动。远处目标仅在画面上隐藏；图上显示收到、雷达报告和当前可见的目标数。超过 1 秒没有新数据时显示等待提示。直接启动：

检测画面按图像采集时间查找融合结果；如果融合消息晚于 YOLO 检测消息到达，
对应帧会重新绘制，匹配成功的框变为绿色。

```bash
source /opt/ros/humble/setup.bash
source install/setup.bash
ros2 run dozer_perception detection_viewer.py
```

无图形桌面时仍可用 `--ros-args -p show_window:=false` 发布带框图像话题。

Jetson Orin（JetPack 6 / CUDA 12.6）上，根目录启动脚本使用工作空间内的
`.venv-gpu`，其中安装匹配的 PyTorch 2.8.0、torchvision 0.23.0 和 cuDNN 9.3。
启动前会检查 CUDA；环境不兼容时会报错退出，不会悄悄退回 CPU 推理。
当前 cuDNN 运行库的卷积引擎在这台 Orin 上不可用，因此节点关闭 cuDNN 卷积，
使用已经验证可运行的 PyTorch 原生 CUDA 卷积；推理仍在 GPU 上执行。
