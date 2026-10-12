# 雷达状态，由驱动约 1 Hz 发布，汇总 SR73F 0x201（RadarState）、0x402（区域状态）、0x700（版本）。

std_msgs/Header header
uint8 sensor_id
bool online                   # 驱动在超时时间内收到该雷达报文
string firmware_version       # 0x700，例如 "1.0.21"；未收到时为空
float32 cycle_rate_hz         # 最近统计窗口内的目标周期频率（正常约 30 Hz）
uint16 max_distance_m
uint8 sort_index
uint8 radar_power
uint8 output_type
uint8 rcs_threshold
uint8 can_baudrate            # 0:500K 1:250K 2:1M
uint8 motion_rx_state
bool nvm_read_ok
bool nvm_write_ok
bool region_active            # 收到 0x402，说明区域过滤已生效
uint8 max_output_number       # 区域内最大输出目标数（region_active 时有效）
