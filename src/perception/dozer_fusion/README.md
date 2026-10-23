# dozer_fusion

`fusion_node.py` 合并三台雷达的目标，并把每个目标投影到三路相机图像中，
与 YOLO 检测框关联。发布 `/perception/fused_detections`，每个雷达目标对应一条结果。
未匹配到任一相机时 `class_id=4294967295`（unknown），`camera_source_id` 为空，
但保留雷达 3D 坐标。匹配后填入类别、置信度、2D 框和相机来源。
3D 位置和速度统一在 `header.frame_id=base_link` 中。

使用前需标定 `dozer_bringup/config/extrinsics.yaml` 中的雷达与相机外参，
并为三路 **image_small** 的实际分辨率分别填写 `config/fusion.yaml` 中的
`fx/fy/cx/cy`。零焦距会拒绝启动。输入图像需要预先去畸变或校正；
SR73F 不提供 z，投影时假设雷达 z=0，因此输出的是雷达点位置，并非物体尺寸。
雷达周期超过 `stale_sec` 不进入输出；相机检测与雷达周期相差超过
`max_age_sec` 不进行关联。同一相机内按检测框中心距离一对一匹配；
跨相机若多次匹配同一雷达目标，采用置信度最高的检测。TF 使用雷达时间戳。

配置模型和标定后运行：
`ros2 launch dozer_bringup dozer.launch.py use_radar:=true use_perception:=true use_fusion:=true`。
如果已有检测发布节点，可单独启用 `use_fusion:=true`。
