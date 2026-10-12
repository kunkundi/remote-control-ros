# 实时驾驶指令，字段与 rtc 协议 vtsrtc.vehicle.v1.DriveCommand 对齐。
# 只保留最新值：QoS 使用 best_effort + keep_last(1)。
# vehicle 节点按收到时间做看门狗（默认 300 ms），超时本地停车。

uint8 DRIVE_STOP=0
uint8 DRIVE_FORWARD=1
uint8 DRIVE_REVERSE=2
uint8 STEER_CENTER=0
uint8 STEER_LEFT=1
uint8 STEER_RIGHT=2

std_msgs/Header header
uint64 seq                    # 远端发送序号，必须单调递增
string remote_session_id      # 发出该指令的远程驾驶台会话
uint8 drive_direction
uint8 steering_direction
float32 throttle              # [0, 1]
float32 brake                 # [0, 1]
