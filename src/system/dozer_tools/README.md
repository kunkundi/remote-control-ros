# dozer_tools

开发工具。

| 工具 | 用法 |
|---|---|
| `mock_radar_publisher.py` | `ros2 run dozer_tools mock_radar_publisher.py --ros-args -p radars:="['front_left','front_right']"` |
| `record_bag.sh` | `ros2 run dozer_tools record_bag.sh [输出目录]`，MCAP 格式，不录原始图像 |
# mock_camera_publisher.py

发布前左、前右、后向三路 640x360 纯色 BGR 图像到
`/camera/<name>/image_small`，用于没有相机驱动时的感知话题联调。
默认 1 Hz；纯色图不用于验证 YOLO 检出率。
