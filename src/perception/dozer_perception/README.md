# dozer_perception

感知算法节点（感知层），默认通过 edge 共享内存获取图像，结果继续走 ROS，不链接驱动代码。负责人：待定。

- 输入：`/dev/shm/dozer_camera_<name>` 的最新 I420 帧，支持 front_left、front_right、front、rear、left、right；`image_backend=ros` 时回退到 `/camera/<name>/image_small`
- 输出：`/perception/<name>/detections`（Detection2DArray，header.stamp 等于输入帧时间戳）
- 实现：`yolo_node.py` 使用 Ultralytics YOLO；在 ROS Python 环境中安装
  `ultralytics` 及对应平台推理依赖。车端默认 `device=0`，要求 CUDA 可用；
  只处理最新帧，慢推理不反压采集；60 Hz 轮询只触发新序号，实际推理速率由模型吞吐决定。每个 YOLO 节点每 5 秒打印处理帧数、`model.predict()` 平均/最大耗时和模型内部推理平均耗时（毫秒）。
- 共享内存消费端先拷贝 I420 快照并释放锁，再转换 BGR 和推理。默认拒绝发布后超过 200 ms 的帧；producer 重启后自动跟随新文件。相机标识必须与 `source_id` 一致。结果保留输入时间戳和生产者帧序号，不发布中间 ROS Image。
- ROS 回退支持 `rgb8`、`bgr8`、`rgba8`、`bgra8`，输出时间戳沿用输入图像时间。
- 模型文件放在本包的 `model/` 目录，共享内存模式下一至六路共用 `best_int8_batch6.engine`（固定 batch=6）；原始 `best.pt` 和单路 `best_int8.engine` 保留；
  `dozer_bringup/config/perception.yaml` 已将相机节点指向该文件。
  模型文件不入 Git；INT8 engine 由当前 Jetson 生成，换设备或 TensorRT 版本需重新导出。当前校准图片来自室内场景，部署前仍需用实际作业场景验证精度。

`start_all.sh` 默认启动六路 YOLO 和三台雷达且不开窗口；`start_detection_viewer.sh` 默认启动六路 YOLO、前左雷达和预览窗口。通过 launch 的 `perception_cameras:=front_left,front_right,front,rear,left,right` 或根脚本的 `--perception-cameras front_left,front_right,front,rear,left,right` 启用六路，也可选择任意非空子集；改变运行中的名单须显式 `--restart`。共享内存模式下一至六路只启动一个 YOLO 批推理进程。批推理处理最新帧，最多等待 40 ms 集齐相机，不足六帧时补齐输入并只发布真实帧；因此少于六路也会执行六张图的计算。批量推理按预处理、TensorRT、后处理、发布的顺序执行。预览显示所选相机，雷达窗口只显示前左雷达；融合按配对配置处理三台雷达和六路相机。

`detection_viewer.py` 将所选相机的检测框叠加到对应时间戳的图像上，并在匹配到雷达目标时显示其 3D 坐标。绿色框表示已融合，黄色框表示仅 YOLO 检测。画面顶部显示 YOLO 数量及未分类雷达目标数量。仅显式启用 `publish_annotated` 时发布 `/perception/front_left/annotated_image` 和 `/perception/front_right/annotated_image`；有图形桌面时显示相机合并窗口及独立的前左雷达俯视图窗口。雷达图直接使用 `/radar/front_left/objects` 的坐标：前方朝上、左侧朝左，显示前方 0–6 m、左右各 3 m，网格间隔 1 m；点旁数字为雷达目标 ID，绿色点表示相对速度接近零，橙色点表示运动。远处目标仅在画面上隐藏；图上显示收到、雷达报告和当前可见的目标数。超过 1 秒没有新数据时显示等待提示。直接启动：

检测画面按图像采集时间查找融合结果；如果融合消息晚于 YOLO 检测消息到达，
对应帧会重新绘制，匹配成功的框变为绿色。

```bash
source /opt/ros/humble/setup.bash
source install/setup.bash
ros2 run dozer_perception detection_viewer.py
```

默认不创建图像订阅或带框图像发布者。上文的 annotated_image 话题仅在显式设置 `--ros-args -p publish_annotated:=true` 时启用；无图形桌面调试可再加 `-p show_window:=false`。默认 SHM 模式需先运行直采 edge，且使用同一用户。

预览独立读取最新帧并缓存 50 帧，严格按时间戳匹配；读取遗漏了某个推理帧时不会把框画到其他画面，可能跳过该次预览。检测先于图像到达时会在图像到达后补绘。相机或检测超过 0.5 秒无新数据时分别显示失效提示，不把旧框当作实时结果。原始时间戳是软件出队时间，不是硬件同步曝光时间。

Jetson Orin（JetPack 6 / CUDA 12.6）上，根目录启动脚本使用工作空间内的
`.venv-gpu`，其中安装匹配的 PyTorch 2.8.0、torchvision 0.23.0 和 cuDNN 9.3。
启动前会检查 CUDA；环境不兼容时会报错退出，不会悄悄退回 CPU 推理。
当前 cuDNN 运行库的卷积引擎在这台 Orin 上不可用，因此节点关闭 cuDNN 卷积，
使用已经验证可运行的 PyTorch 原生 CUDA 卷积；推理仍在 GPU 上执行。
