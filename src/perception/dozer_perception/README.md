# dozer_perception

感知算法节点（感知层），只通过 topic 获取传感器数据，不链接驱动代码。负责人：待定。

- 输入：当前两路 `/camera/<name>/image_small`（sensor_msgs/Image）
- 输出：前左 `/perception/detections`，前右 `/perception/front_right/detections`（Detection2DArray，header.stamp 等于输入帧时间戳）
- 实现：`yolo_node.py` 使用 Ultralytics YOLO；在 ROS Python 环境中安装
  `ultralytics` 及对应平台推理依赖。
- 支持 `rgb8`、`bgr8`、`rgba8`、`bgra8`，输出时间戳沿用输入图像采集时间。
- 模型文件放在本包的 `model/` 目录，默认文件名为 `best.pt`；
  `dozer_bringup/config/perception.yaml` 已将相机节点指向该文件。
  权重文件不入 Git，需自行放入实际模型。

`detection_viewer.py` 将两路检测框叠加到对应时间戳的图像上，并在匹配到雷达目标时显示其 3D 坐标。绿色框表示已融合，黄色框表示仅 YOLO 检测。画面顶部显示 YOLO 数量及未分类雷达目标数量。它发布 `/perception/front_left/annotated_image` 和 `/perception/front_right/annotated_image`；有图形桌面时显示合并窗口。直接启动：

```bash
source /opt/ros/humble/setup.bash
source install/setup.bash
ros2 run dozer_perception detection_viewer.py
```

无图形桌面时仍可用 `--ros-args -p show_window:=false` 发布带框图像话题。
