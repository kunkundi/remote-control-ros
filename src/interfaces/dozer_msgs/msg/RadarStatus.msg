# 雷达状态，对应 SR73F 0x201 RadarState，雷达约 1 Hz 发送。

std_msgs/Header header
uint8 sensor_id
bool online                   # 驱动在超时时间内收到该雷达报文
uint16 max_distance_m
uint8 sort_index
uint8 radar_power
uint8 output_type
uint8 rcs_threshold
uint8 can_baudrate            # 原始编码，见 SR73F 协议
bool nvm_read_ok
bool nvm_write_ok
