# dozer_perception

感知算法节点（感知层），只通过 topic 获取传感器数据，不链接驱动代码。负责人：待定。

- 输入：`/camera/<name>/image_small`（sensor_msgs/Image）
- 输出：`/perception/detections`（Detection2DArray，header.stamp 等于输入帧时间戳）
- 来源：由 rtc-framework `client/modules/vision/` 中的 YOLO（TensorRT）迁移而来，迁移前先在 rtc-framework 内完成解耦。
- 模型与 engine 文件不入库，路径通过参数配置。
