# dozer_msgs

车端内部接口定义，所有模块的公共契约。修改规则见工作空间 README 第 4.4 节。

| 文件 | 用途 |
|---|---|
| `AudioFrame` | 带时间戳的交错有符号 PCM 帧，供音频驱动发布和远程驾驶订阅 |
| `RadarObject` / `RadarObjectArray` | 雷达单周期目标列表（SR73F 0x60A/0x60B） |
| `RadarStatus` | 雷达状态（SR73F 0x201/0x402/0x700 汇总，含在线、固件、周期频率） |
| `Detection2D` / `Detection2DArray` | 图像 2D 检测结果 |
| `DriveCommand` | 实时驾驶指令（对齐 vtsrtc.vehicle.v1.DriveCommand） |
| `VehicleState` | 车辆控制状态（对齐 vtsrtc.vehicle.v1.VehicleState） |
| `srv/SetGear` | 换档事务（对齐 SetGear / EventAck） |

## 音频格式

`AudioFrame` 的 `frame_count` 为每声道采样数，`data` 长度必须等于
`frame_count * channels * bits_per_sample / 8`。当前采集与 RTC 输入约定为
10 ms、16 位有符号交错 PCM、1 或 2 声道，支持 8000、16000、32000、44100、48000 Hz。
字节序采用部署平台原生序（Jetson 为小端）；跨不同字节序平台时需显式转换。
`header.stamp` 是完整帧发布时的 ROS 时间，不是硬件采样时间。
默认话题 `/audio/capture`，QoS 为 SensorData、KeepLast(2)。
