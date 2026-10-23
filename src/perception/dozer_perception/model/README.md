# YOLO 模型目录

当前 `best.pt` 是 Ultralytics 官方 YOLO11n 通用目标检测权重（COCO 80 类），
来源：<https://github.com/ultralytics/assets/releases/download/v8.3.0/yolo11n.pt>。
下载文件 SHA-256：`0ebbc80d4a7680d14987a577cd21342b65ecfd94632bd9a8da63ae6417644ee1`。

若使用自己训练的权重，可直接替换 `best.pt`。也可以使用 TensorRT
`.engine` 文件，但需同步修改 `dozer_bringup/config/perception.yaml` 中
三个节点的 `model_path`。模型权重不提交到 Git。
