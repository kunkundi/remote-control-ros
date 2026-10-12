# dozer_msgs

车端内部接口定义，所有模块的公共契约。修改规则见工作空间 README 第 4.4 节。

| 文件 | 用途 |
|---|---|
| `RadarObject` / `RadarObjectArray` | 雷达单周期目标列表（SR73F 0x60A/0x60B） |
| `RadarStatus` | 雷达状态（SR73F 0x201/0x402/0x700 汇总，含在线、固件、周期频率） |
| `Detection2D` / `Detection2DArray` | 图像 2D 检测结果 |
| `DriveCommand` | 实时驾驶指令（对齐 vtsrtc.vehicle.v1.DriveCommand） |
| `VehicleState` | 车辆控制状态（对齐 vtsrtc.vehicle.v1.VehicleState） |
| `srv/SetGear` | 换档事务（对齐 SetGear / EventAck） |
