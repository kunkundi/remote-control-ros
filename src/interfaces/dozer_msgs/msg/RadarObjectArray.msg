# 一台雷达一个测量周期的目标列表。
# header.stamp：收到该周期 0x60A 列表头的时刻（系统时钟）。
# header.frame_id：雷达坐标系名，例如 radar_front_left，外参由 tf 提供。

std_msgs/Header header
uint8 sensor_id               # 雷达 SensorID（0-7），决定 CAN 报文 ID 偏移
uint16 meas_count             # 雷达测量周期计数（0x60A Objects_MeasCount），用于检测丢周期
uint8 nof_objects_reported    # 0x60A 声明的目标数；与 objects 长度不一致说明本周期有丢帧
RadarObject[] objects
