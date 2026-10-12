# 单个 2D 检测框，坐标为相对原图的归一化值 [0, 1]，原点在左上角。

uint32 class_id
float32 score                 # 置信度 [0, 1]
float32 x                     # 左上角 x
float32 y                     # 左上角 y
float32 w
float32 h
bool has_track_id
uint32 track_id
