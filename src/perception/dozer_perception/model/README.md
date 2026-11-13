# YOLO 模型目录

保留的 `best.pt` 是 Ultralytics 官方 YOLO11n 通用目标检测权重（COCO 80 类），
来源：<https://github.com/ultralytics/assets/releases/download/v8.3.0/yolo11n.pt>。
下载文件 SHA-256：`0ebbc80d4a7680d14987a577cd21342b65ecfd94632bd9a8da63ae6417644ee1`。

若使用自己训练的权重，可直接替换 `best.pt`。也可以使用 TensorRT
`.engine` 文件，但需同步修改 `dozer_bringup/config/perception.yaml` 中
六个节点的 `model_path`。模型权重不提交到 Git。

当前运行配置使用 `best_int8.engine`，由本机 Jetson Orin 的 TensorRT 10.3 从 `best.pt` 导出；转换脚本、校准图片、校准缓存和基准结果保存在 `/home/nvidia/workspace/lxy/`。校准图片是室内办公场景，不能代替实际作业环境的精度验证。切回原模型时，将 `dozer_bringup/config/perception.yaml` 中六路 `model_path` 改回 `best.pt`。

六路批推理使用固定 batch=6 的 `best_int8_batch6.engine`（TensorRT 10.3，同一 Jetson 上由 360 张室内图片校准）。构建命令与日志位于 `/home/nvidia/workspace/lxy/`。批量 engine 不能代替真实作业场景的精度验证。
