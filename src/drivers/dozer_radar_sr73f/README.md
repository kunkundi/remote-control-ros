# dozer_radar_sr73f

SR73F 毫米波雷达驱动（驱动层），遵守工作空间 README 第 5 节驱动层规范。负责人：待定。

- 输入：SocketCAN `can0`（1 Mbps，规划 3~4 台，SensorID 0-3，报文 ID = 基础 ID + SensorID × 0x10）
- 输出：`/radar/<name>/objects`（RadarObjectArray，30 Hz）、`/radar/<name>/status`（RadarStatus，1 Hz）、`/diagnostics`
- 参数：`dozer_bringup/config/radar.yaml`
- 协议资料：`autoDozer/driver/homibo/`（协议 PDF、DBC）

实现要点：
1. 直接使用 SocketCAN（`socket(PF_CAN, SOCK_RAW, CAN_RAW)`），按雷达 ID 设置接收过滤器，不依赖 ros2_socketcan。
2. 以 0x60A 列表头划分周期，收齐 `NofObjects` 个 0x60B 或收到下一个 0x60A 时发布；数量不一致计入诊断。
3. 报文解析写成与 ROS 无关的类，用固定字节的 gtest 测试覆盖。
4. 横向距离正方向需实测确认，统一转换为 REP-103（y 左为正）。
5. 雷达配置（0x200/0x401：ID、波特率、最大目标数、区域）做成单独的命令行工具，不在节点启动时自动下发。
