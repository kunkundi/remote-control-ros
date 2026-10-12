# 一帧图像的检测结果。
# header.stamp 必须等于输入图像帧的采集时间戳（不是推理完成时间），
# rtc_bridge 依赖它把检测框与视频帧对齐。

std_msgs/Header header
string source_id              # 相机/视频源 ID，与 rtc 视频源 ID 一致，例如 merged_image
uint64 frame_seq              # 输入帧序号
uint32 image_width
uint32 image_height
string model_name
Detection2D[] detections
