# YOLO 模型目录

保留的 `best.pt` 是 Ultralytics 官方 YOLO11n 通用目标检测权重（COCO 80 类），
来源：<https://github.com/ultralytics/assets/releases/download/v8.3.0/yolo11n.pt>。
下载文件 SHA-256：`0ebbc80d4a7680d14987a577cd21342b65ecfd94632bd9a8da63ae6417644ee1`。

若使用自己训练的权重，可替换模型文件并同步修改
`dozer_bringup/config/perception.yaml` 中相应节点的 `model_path`。
仓库包含 `best.pt`、单路 `best_int8.engine` 和六路 `best_int8_batch6.engine`。节点从已安装的 `share/dozer_perception/model/` 读取引擎。

如需重新导出，在本包的 `tools/` 下用 `capture_calibration.py` 从共享内存采集校准帧，再执行 `export_trt.py int8 --data ../model/exports/calibration/data.yaml --batch 6`。单路使用 `--batch 1`。脚本将生成文件写入 `model/exports/`，可用 `verify_engine.py ../model/best_int8_batch6.engine --batch 6` 检查六路引擎。

六路批推理引擎固定 batch=6，由本机 Jetson Orin 的 TensorRT 10.3 从 `best.pt` 导出，并用 360 张室内图片校准。校准图片不能代替实际作业场景的精度验证；在其他设备上部署时需验证引擎兼容性。
