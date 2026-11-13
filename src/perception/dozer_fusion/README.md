# dozer_fusion

`fusion_node.py` 合并三台雷达的目标，并把每个目标投影到配置允许的六路相机图像中，
与 YOLO 检测框关联。发布 `/perception/fused_detections`：每个雷达目标一条结果，未匹配雷达的相机检测框也各有一条。
未匹配到任一相机时 `class_id=4294967295`（unknown），`camera_source_id` 为空，
但保留雷达 3D 坐标与 `radar_source_id`。匹配后填入类别、置信度、2D 框和相机来源。
纯相机目标的 `has_3d=false`、`radar_source_id` 为空；其 3D 数值字段无效。
有 3D 的位置和速度统一在 `header.frame_id=base_link` 中。

使用前需标定 `dozer_bringup/config/extrinsics.yaml` 中的雷达与相机外参，
并为六路 **image_small** 的实际分辨率分别填写 `config/fusion.yaml` 中的
`fx/fy/cx/cy`。零焦距会拒绝启动。输入图像需要预先去畸变或校正；
SR73F 不提供目标高度，投影时假设雷达 z=0，因此输出的是雷达点位置，并非物体尺寸。
雷达周期超过 `stale_sec` 不进入输出；相机检测与雷达周期相差超过
`max_age_sec` 不进行关联。关联时只要求投影横坐标落在检测框的横向范围内，
不限制图像纵坐标；同一相机内按横向中心距离一对一匹配。
这能容忍未知目标高度，但同一竖线上的不同目标可能误匹配，且单目检测框无法验证雷达深度。
`max_match_distance_m` 可限制参与视觉关联的雷达前向距离；当前联调配置为 6 m，
不影响雷达原始目标话题或未分类融合目标输出。
跨相机若多次匹配同一雷达目标，采用置信度最高的检测。TF 使用雷达时间戳。
YOLO 推理完成后，节点按图像的采集时间从最近约 30 秒的雷达历史帧中选择时间最接近的周期；
这样推理延迟不会被误认为传感器采集时间差。此时额外发布一条以图像采集时间为
`header.stamp` 的融合结果，供对应图像显示；周期性的雷达快照仍照常发布。融合节点每 5 秒分别打印图像触发与周期快照的消息数、平均/最大计算和发布耗时（毫秒）。

配置模型和标定后运行：
`ros2 launch dozer_bringup dozer.launch.py use_radar:=true use_perception:=true use_fusion:=true`。
如果已有检测发布节点，可单独启用 `use_fusion:=true`。

当前配对：前左雷达→前左、前右、左相机；前右雷达→前左、前右、右相机；后雷达→后、左、右相机。`radar_camera_pairs` 使用雷达 `frame_id` 限定可关联的相机。三个雷达已列入 `radar_names`，但 SensorID、物理方向、相机内外参均需实机核对；新增相机的内外参为占位值，匹配结果尚不可用于实车判断。
