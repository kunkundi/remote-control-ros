# dozer_radar_sr73f

纳雷 SR73F 毫米波雷达驱动（驱动层），遵守工作空间 README 第 5 节驱动层规范。负责人：待定。

- 一条 CAN 总线（默认 `can0`）上的多台雷达（SensorID 0-7），按雷达分别发布
- 直接使用 SocketCAN，不依赖 USBCAN 盒子或 `ros2_socketcan`
- 协议依据：`autoDozer/driver/homibo/` 下的《SR73F 毫米波雷达协议 1.7》与 `SR73F_CAN_database_ID0.dbc`

## 组成

| 文件 | 说明 |
|---|---|
| `sr73f_protocol.*` | 协议编解码，与 ROS、硬件无关 |
| `object_list_assembler.*` | 把 0x60A 列表头与 0x60B 目标拼成完整周期 |
| `can_frame_source.*` | SocketCAN 收发（内核时间戳、错误帧统计）与 candump 日志回放 |
| `radar_sr73f_node.cpp` | 驱动节点（component，同时生成可执行文件 `radar_sr73f_node`） |
| `sr73f_config_main.cpp` | 配置与排查工具 `sr73f_config`（不依赖 ROS） |
| `scripts/gen_sr73f_candump.py` | 生成模拟报文（candump 格式），无硬件时测试用 |

## 输出

| Topic | 类型 | QoS | 说明 |
|---|---|---|---|
| `/radar/<name>/objects` | `dozer_msgs/RadarObjectArray` | SensorData | 每个测量周期一帧，约 30 Hz |
| `/radar/<name>/points` | `sensor_msgs/PointCloud2` | SensorData | 字段 x,y,z,vx,vy,rcs,id；只在有订阅者时发布，供 rviz/Foxglove 查看 |
| `/radar/<name>/status` | `dozer_msgs/RadarStatus` | Reliable | 1 Hz：在线、固件、周期频率、波特率、区域过滤等 |
| `/diagnostics` | `diagnostic_msgs/DiagnosticArray` | 默认 | 每台雷达一项，另有一项总线状态 |

- `header.stamp`：收到该周期 0x60A 列表头的**内核接收时间**（`SO_TIMESTAMPNS`，系统时钟）。
- `header.frame_id`：参数 `<name>.frame_id`，外参在 `dozer_bringup/config/extrinsics.yaml`。

## 坐标系

SR73F 原始坐标：纵向向前为正，**横向向右为正**（协议：点 1 为右下角且 `Point1Lat > Point2Lat`；目标从左往右横穿横向速度为正）。
驱动默认（`flip_lateral: true`）把横向距离与横向速度取反，输出 REP-103 坐标：x 前、y 左。
**这一点需要实车确认**（见“硬件验证”第 4 步）。

## 参数（`dozer_bringup/config/radar.yaml`）

| 参数 | 默认 | 说明 |
|---|---|---|
| `can_interface` | `can0` | CAN 接口 |
| `radar_names` | `["front_left"]` | 启用的雷达名，决定 topic 名 |
| `<name>.sensor_id` | 序号 | 雷达 SensorID（0-7） |
| `<name>.frame_id` | `radar_<name>` | 坐标系名 |
| `flip_lateral` | `true` | 横向取反（右正 → 左正） |
| `publish_pointcloud` | `true` | 有订阅者时发布点云 |
| `offline_timeout_ms` | 500 | 超过该时间没有报文判为离线 |
| `cycle_timeout_ms` | 100 | 0x60A 之后等待目标收齐的最长时间 |
| `replay_file` | `""` | 非空时回放 candump -l 日志，不读 CAN |
| `replay_loop` | `true` | 回放结束后循环 |

## 诊断说明

| 现象 | 含义 |
|---|---|
| 未收到报文 / 离线 | 雷达未上电、接线错误、波特率不一致，或 SensorID 与配置不符 |
| 0x201 状态报文过多 | 5 秒内超过 7 帧，多台雷达使用了同一 SensorID |
| SensorID 与配置不一致 | 雷达 0x201 上报的 ID 与参数不符 |
| 目标周期频率偏低 | 低于 20 Hz |
| 存在不完整周期 | 收到的 0x60B 少于 0x60A 声明数量，多见于总线过载或丢帧 |
| 总线错误帧 | 波特率不一致、终端电阻缺失、接线问题 |
| 未配置的 SensorID | 总线上有雷达的 ID 不在 `radar_names` 中 |

---

## 硬件验证步骤

以下命令在 Jetson 上执行，先 `cd ~/workspace/ros2_ws && source install/setup.bash`。
为书写方便，记 `C=install/dozer_radar_sr73f/lib/dozer_radar_sr73f/sr73f_config`。

### 0. 加载 SocketCAN 模块（只需一次）

当前系统开机时没有加载 `can`、`can_raw` 模块，任何 SocketCAN 程序（包括 `candump`）都会报
`Address family not supported by protocol`。

```bash
sudo modprobe can_raw                     # 会自动加载 can
lsmod | grep -E "^can"                    # 应看到 can_raw、can
# 开机自动加载：
printf 'can\ncan_raw\n' | sudo tee /etc/modules-load.d/can.conf
```

### 1. 接线（先接一台）

| 雷达线色 | 定义 | 接到 |
|---|---|---|
| 红 | 电源 9–24 V DC | 电源正 |
| 黑 | GND | 电源地（与 Jetson 共地） |
| 黄 | CAN_H | can0 的 CAN_H |
| 白/绿 | CAN_L | can0 的 CAN_L |

- 总线两端各需要一个 120 Ω 终端电阻；用万用表在断电时测 CAN_H–CAN_L 之间应约为 60 Ω。
- can0 对应载板上哪个接口，以载板手册为准（`spec/` 目录）。
- 出厂雷达：SensorID 0，500 kbps；can0 当前也是 500 kbps。

### 2. 看原始报文

```bash
candump can0          # 应看到 60A、60B 持续刷新，201、700 约每秒一次
```

没有报文时：`ip -details -statistics link show can0` 看 `state`（ERROR-PASSIVE/BUS-OFF 说明接线或波特率有问题）和错误计数。

### 3. 扫描雷达

```bash
$C scan               # 应列出 ID 0、固件版本、波特率 500K、周期约 30/s
```

### 4. 启动驱动并确认方向

```bash
ros2 launch dozer_bringup radar.launch.py
ros2 topic hz /radar/front_left/objects                 # 约 30 Hz
ros2 topic echo /radar/front_left/objects --field objects
ros2 topic echo /radar/front_left/status
```

方向确认：在雷达**左前方**放一个明显目标（人或金属板），对应目标的 `y` 应为**正值**；
向远离雷达方向移动时 `vx` 为正。若左右相反，告知开发人员（或临时设置 `flip_lateral:=false`）。

可视化：`ros2 launch foxglove_bridge foxglove_bridge_launch.xml` 后用 Foxglove 连接
`ws://<Jetson IP>:8765`，添加 3D 面板查看 `/radar/front_left/points`；需要坐标系时同时运行
`ros2 launch dozer_bringup static_tf.launch.py`。

### 5. 多台雷达：逐台修改 SensorID

**同一时间总线上只能接一台出厂（ID 0）雷达**，否则改 ID 会同时改掉所有雷达。

```bash
# 只接第 2 台雷达：
$C set-id --from 0 --to 1      # 工具会先确认 ID 0 在线且没有重复，再等待新 ID 上报
# 只接第 3 台：  $C set-id --from 0 --to 2
# 只接第 4 台：  $C set-id --from 0 --to 3
# 全部接上：
$C scan                        # 应看到 ID 0-3
```

然后修改 `config/radar.yaml` 的 `radar_names`（例如 `["front_left","front_right","rear_left","rear_right"]`），
重新 `colcon build --packages-select dozer_bringup`（或直接用 `--ros-args -p` 覆盖）。

### 6. 限制每台的输出目标数

4 台满负载（每台 63 个目标）在 500 kbps 下约 190% 负载，必须限制。每台：

```bash
$C set-region --id 0 --max-objects 30 --p1 0,20 --p2 40,-20   # 前方 40 m、左右各 20 m
```

坐标为雷达原始坐标（横向右正），点 1 为右下角、点 2 为左上角。生效后 `scan` 的“区域”列显示 `启用,max=30`。
恢复：`$C disable-region --id 0`。

### 7. 切换到 1 Mbps（全部雷达改完 ID 后）

```bash
for id in 0 1 2 3; do $C set-baud --id $id --baud 1m; done
sudo ip link set can0 down
sudo ip link set can0 up type can bitrate 1000000 restart-ms 100
$C scan                         # 波特率列应为 1M
```

确认无误后，把 `/opt/nx_port_init/nx_can_init.sh` 中 can0 的 `bitrate 500000` 改为
`bitrate 1000000 restart-ms 100`，保证重启后生效。

### 8. 录制数据供离线开发

```bash
candump -l can0                 # 生成 candump-<日期>.log，可用 replay_file 回放
ros2 run dozer_tools record_bag.sh
```

---

## 无硬件测试

```bash
ros2 run dozer_radar_sr73f gen_sr73f_candump.py -o /tmp/sim.log --sensor-ids 0,1,2,3 --objects 20 --seconds 10
ros2 launch dozer_bringup radar.launch.py replay_file:=/tmp/sim.log
colcon test --packages-select dozer_radar_sr73f && colcon test-result --verbose
```

单元测试覆盖：协议文档 7.1/7.3/7.4/7.5 节的示例报文（逐字节）、与厂商 demo 解析公式的 2000 组随机对照、
0x201/0x402 字段位置、周期拼装（丢帧、超时、孤立目标、计数回绕）、candump 解析。

已在 Jetson 上回放验证：4 台 × 63 目标（约 7700 帧/s）时节点占单核约 5%，无丢周期。

## 待确认事项

1. 横向正方向（见“坐标系”），需实车确认。
2. 协议文档称 0x60A 第 4–5 字节为雷达估计的车速，但与 DBC（DLC=4）及接口版本字段位置冲突，暂不解析。
3. 0x300/0x301（外部车速、横摆角速度输入）未实现，当前不需要。
