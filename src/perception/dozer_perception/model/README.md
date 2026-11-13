# YOLO 模型目录

保留的 `best.pt` 是 Ultralytics 官方 YOLO11n 通用目标检测权重（COCO 80 类），
来源：<https://github.com/ultralytics/assets/releases/download/v8.3.0/yolo11n.pt>。
下载文件 SHA-256：`0ebbc80d4a7680d14987a577cd21342b65ecfd94632bd9a8da63ae6417644ee1`。

若使用自己训练的权重，可替换模型文件并同步修改
`dozer_bringup/config/perception.yaml` 中相应节点的 `model_path`。
本仓库跟踪六路批推理引擎 `best_int8_batch6.engine`；其他模型文件默认不提交。

当前共享内存批量推理配置使用 `best_int8_batch6.engine`。单路 ROS 图像模式仍配置为 `best_int8.engine`，该文件不随仓库提交。转换脚本、校准图片、校准缓存和基准结果保存在 `/home/nvidia/workspace/lxy/`。

六路批推理引擎固定 batch=6，由本机 Jetson Orin 的 TensorRT 10.3 从 `best.pt` 导出，并用 360 张室内图片校准。校准图片不能代替实际作业场景的精度验证；在其他设备上部署时需验证引擎兼容性。
