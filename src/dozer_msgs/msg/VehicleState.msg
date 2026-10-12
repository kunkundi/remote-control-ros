# 车辆控制状态反馈，字段与 vtsrtc.vehicle.v1.VehicleState 对齐。

uint8 GEAR_NEUTRAL=0
uint8 GEAR_FORWARD=1
uint8 GEAR_REVERSE=2

std_msgs/Header header
uint8 active_gear
uint64 last_received_drive_seq
bool watchdog_stopped
bool interface_ready          # 底层车辆接口（CAN/串口/SDK）已就绪
