# autoDozer 车端 ROS 2 工作空间

本目录是推土机远程驾驶平台车端（Jetson Orin NX）的 ROS 2 工作空间，负责传感器采集、感知算法、结果上传与远程控制下行。
本 README 是各模块开发的**统一约定**，新增或改造模块前请先通读；对约定的修改需要评审（见“协作流程”）。

- 平台：Jetson Orin NX 8GB，JetPack 6（L4T R36.4），Ubuntu 22.04
- ROS：Humble，RMW 为 Fast DDS（`rmw_fastrtps_cpp`）
- 语言：C++17（ROS 节点），Python 3.10（工具与 launch）

---

## 1. 整体架构

```text
┌──────────────────────────── Jetson Orin NX ────────────────────────────┐
│                                                                        │
│  [dozer_rtc_bridge]  进程内：相机采集 → 格式转换 → NVENC → WebRTC 视频   │
│     │  发布 /camera/<name>/image_small（降采样，供算法）                 │
│     │  订阅 /perception/* /radar/* /vehicle/state → nanopb → DataChannel │ ──► 远程驾驶台
│     │  DataChannel 控制下行 → /vehicle/drive_cmd、/vehicle/set_gear      │ ◄── (rtc_console)
│     ▼                                                                  │
│  [dozer_perception]  /camera/*/image_small → /perception/detections     │
│  [dozer_radar]       can0 (SR73F × 3~4) → /radar/<name>/objects          │
│  [dozer_fusion]      雷达 + 视觉 → /fusion/objects（后期）                │
│  [dozer_vehicle]     /vehicle/drive_cmd → 看门狗 → CAN/串口 → 车辆        │
│                      → /vehicle/state                                   │
└────────────────────────────────────────────────────────────────────────┘
```

设计原则：

1. **视频热路径不走 DDS**：相机采集 → 编码 → WebRTC 留在 `dozer_rtc_bridge` 进程内零拷贝完成；只有降采样帧发布到 topic 给算法使用。
2. **对内 ROS 消息，对外 nanopb 协议**：车端模块之间只用 `dozer_msgs`；车端 ↔ 驾驶台继续使用 rtc-framework 中已有的 nanopb 协议（`client/protocols/`），二者之间只由 `dozer_rtc_bridge` 翻译。
3. **控制链路独立**：车辆控制在独立进程 `dozer_vehicle` 中，看门狗在车辆侧判定；算法或网关崩溃不影响本地安全停车。
4. **进程隔离**：驱动、算法、网关、控制分进程运行，单个模块崩溃不拖垮其它模块。

---

## 2. 目录结构

```text
~/workspace/
├── rtc-framework/              # 现有 RTC 工程（xmake 构建），保持独立，不复制进本工作空间
│   └── install/                # xmake install 产物：libvtsrtc、rtc_* 静态库与头文件
└── ros2_ws/                    # 本工作空间（git 仓库）
    ├── README.md               # 本文件：统一约定
    └── src/
        ├── dozer_msgs/         # 接口定义（msg/srv），修改需全员评审
        ├── dozer_bringup/      # launch、config/*.yaml、传感器外参、systemd
        ├── dozer_radar/        # SR73F 毫米波雷达驱动（SocketCAN）
        ├── dozer_perception/   # 感知算法（YOLO/TensorRT 等）
        ├── dozer_fusion/       # 多传感器融合（后期）
        ├── dozer_vehicle/      # 车辆控制与安全看门狗
        ├── dozer_rtc_bridge/   # RTC 网关（由 rtc_edge 改造而来）
        └── dozer_tools/        # 模拟数据发布器、录包脚本等开发工具
```

单个 C++ 包内部结构：

```text
dozer_xxx/
├── CMakeLists.txt
├── package.xml
├── README.md            # 职责、订阅/发布的 topic、参数、负责人
├── include/dozer_xxx/   # 公共头文件
├── src/                 # 实现；节点入口为 xxx_node.cpp
└── test/                # gtest 单元测试
```

参数文件统一放在 `dozer_bringup/config/<模块>.yaml`，不放在各模块包内，便于整车配置集中管理。

---

## 3. 模块规划与负责人

| 包 | 职责 | 输入 | 输出 | 负责人 | 状态 |
|---|---|---|---|---|---|
| `dozer_msgs` | 接口定义 | — | — | 架构负责人 | 第一版已提交 |
| `dozer_bringup` | 启动编排、参数、外参 | — | `/tf_static` | 集成负责人 | 骨架 |
| `dozer_radar` | SR73F 驱动，can0，1 Mbps，SensorID 0-3 | can0 | `/radar/<name>/objects`、`/radar/<name>/status` | 待定 | 骨架 |
| `dozer_perception` | 检测/感知算法 | `/camera/<name>/image_small` | `/perception/detections` | 待定 | 骨架 |
| `dozer_fusion` | 雷达 + 视觉融合 | 雷达、检测、tf | `/fusion/objects` | 待定 | 后期 |
| `dozer_vehicle` | 驾驶指令看门狗、换档、车辆接口 | `/vehicle/drive_cmd`、`/vehicle/set_gear` | `/vehicle/state` | 待定 | 骨架 |
| `dozer_rtc_bridge` | 相机推流、结果上传、控制下行 | 相机、上述 topic、DataChannel | 视频、DataChannel、`/vehicle/*` | RTC 模块负责人 | 骨架 |
| `dozer_tools` | 模拟数据、录包 | — | — | 集成负责人 | 可用 |

负责人确定后填写到表中和各包的 `package.xml`（`<maintainer>`）中。

---

## 4. 接口约定

### 4.1 Topic

| Topic | 类型 | 发布者 | QoS | 频率 |
|---|---|---|---|---|
| `/radar/<name>/objects` | `dozer_msgs/RadarObjectArray` | dozer_radar | SensorData | 30 Hz |
| `/radar/<name>/status` | `dozer_msgs/RadarStatus` | dozer_radar | Reliable | 1 Hz |
| `/camera/<name>/image_small` | `sensor_msgs/Image` | dozer_rtc_bridge | SensorData | ≤ 15 Hz |
| `/perception/detections` | `dozer_msgs/Detection2DArray` | dozer_perception | SensorData | 跟随输入 |
| `/fusion/objects` | 待定 | dozer_fusion | SensorData | — |
| `/vehicle/drive_cmd` | `dozer_msgs/DriveCommand` | dozer_rtc_bridge | BestEffort, KeepLast(1) | 跟随远端 |
| `/vehicle/set_gear` | `dozer_msgs/srv/SetGear`（服务） | dozer_vehicle 提供 | — | 事件 |
| `/vehicle/state` | `dozer_msgs/VehicleState` | dozer_vehicle | Reliable | 10 Hz |
| `/diagnostics` | `diagnostic_msgs/DiagnosticArray` | 全部模块 | 默认 | 1 Hz |

命名规则：`/<模块>/<实例>/<数据>`，实例名用小写加下划线（如 `front_left`）。

### 4.2 时间戳

- `header.stamp` 填**数据采集时刻**，不是发布时刻。
  - 相机：帧采集时间戳；
  - 雷达：收到该周期 `0x60A` 列表头的时刻；
  - 检测结果：必须等于输入图像帧的时间戳（rtc_bridge 靠它把检测框与视频帧对齐）。
- 统一使用系统时钟（`rclcpp::Clock` 默认 ROS time = system time），不要自行使用 `steady_clock` 填 header。

### 4.3 坐标系

- 遵循 REP-103：x 前、y 左、z 上，单位 m、rad。
- `base_link`：车体坐标系（原点待机械确定）。
- 传感器坐标系：`radar_<name>`、`camera_<name>`。
- 外参只在 `dozer_bringup/config/extrinsics.yaml` 中维护，由 `static_tf.launch.py` 发布，代码中不得写死外参。

### 4.4 消息修改规则

- `dozer_msgs` 是所有模块的公共契约，修改必须通过合并请求，并由所有受影响模块的负责人评审。
- 已投入使用的字段不得改名、改类型或改语义；兼容修改只能新增字段。
- 与 rtc 线协议对应的消息（`DriveCommand`、`VehicleState`、`SetGear`）字段必须与 rtc-framework 的 `.proto` 保持一致。

---

## 5. 包规范

1. ROS 节点使用 C++17、`ament_cmake`；工具脚本可用 Python。
2. 每个节点一个可执行文件，入口为 `src/<名称>_node.cpp`；核心逻辑写成与 ROS 无关的类，便于单元测试。
3. 参数全部通过 ROS 参数声明，默认值写在 `dozer_bringup/config/*.yaml`，代码中不写死设备名、IP 和阈值。
4. 每个模块发布 `/diagnostics`，至少包含在线状态、输入频率和错误计数。
5. 解析外部数据（CAN 报文、DataChannel 载荷）必须做长度、范围和有限值校验，并有对应的 gtest 测试。
6. 代码注释使用中文，仅为不明显的行为、协议约束和安全要求添加注释。
7. 不提交 `build/`、`install/`、`log/`、模型文件、TensorRT engine 和录包数据（见 `.gitignore`）。

---

## 6. rtc 模块改造指引（rtc_edge → dozer_rtc_bridge）

### 6.1 范围

| rtc-framework 内容 | 处理方式 |
|---|---|
| `vtsrtc/`（RTC 库，C ABI） | **不动**，通过 `install/` 产物链接 |
| `signaling-server/`、`client/apps/rtc_console/` | **不动**，不在车端运行 |
| `client/protocols/`（nanopb 线协议） | **不动**，继续作为对外协议 |
| `client/modules/runtime`、`camera`、`audio`、`logging` | **不动**，以静态库形式被 rtc_bridge 链接 |
| `client/apps/rtc_edge/edge_application.*`、`edge_options.*` | **改写**为 `dozer_rtc_bridge` 节点 |
| `client/modules/vision/`（YOLO） | 先在 rtc-framework 内解耦，再迁移到 `dozer_perception` |
| `client/modules/vehicle/` | 迁移到 `dozer_vehicle`，看门狗与校验逻辑保持不变，需安全评审 |

**不要把 rtc-framework 源码整体复制进本工作空间。**

### 6.2 建议顺序

1. **在 rtc-framework 内解耦 YOLO**：`DualCameraStreamingModule` 不再创建 `YoloFrameConsumer`；`YoloFrameConsumer` 通过回调输出检测结果，不直接调用 `RtcBroadcastData`。完成后现有 `rtc_edge` 行为不变。
2. **补全 xmake 安装产物**：保证 rtc_bridge 需要的库与头文件（`rtc_runtime`、`rtc_camera`、`rtc_vision_detection_protocol`、`rtc_vehicle_control_protocol` 等）都能 `xmake install` 到 `install/`。
3. **实现 `dozer_rtc_bridge`**：主循环改为 `rclcpp` 执行器；相机采集推流逻辑照搬；新增订阅 `/perception/detections`、`/radar/*/objects`、`/vehicle/state` 并编码上传。
4. **控制下行**：DataChannel 收到的驾驶指令解码后发布 `/vehicle/drive_cmd`，换档调用 `/vehicle/set_gear` 服务，回执按原协议返回。
5. **迁移 YOLO** 到 `dozer_perception`，迁移车辆控制到 `dozer_vehicle`。

### 6.3 构建衔接

`dozer_rtc_bridge/CMakeLists.txt` 已经写好查找逻辑：默认从 `~/workspace/rtc-framework/install` 查找 `c_rtc.h` 与 `libvtsrtc`，并定义导入目标 `vtsrtc::vtsrtc`。路径不同时用以下命令指定：

```bash
colcon build --packages-select dozer_rtc_bridge \
  --cmake-args -DRTC_FRAMEWORK_INSTALL=/path/to/rtc-framework/install
```

rtc-framework 侧继续遵守其 `AGENTS.md`（C++14、C ABI 不变、nanopb 生成规则等）；ROS 节点用 C++17 编译并链接这些库。

---

## 7. 编译与运行

```bash
cd ~/workspace/ros2_ws
colcon build --symlink-install
source install/setup.bash        # ~/.bashrc 中已配置自动 source

# 只编译某个包
colcon build --packages-select dozer_msgs

# 启动（当前只有外参发布与模拟雷达）
ros2 launch dozer_bringup dozer.launch.py use_mock_radar:=true
ros2 topic hz /radar/front_left/objects

# 录包（MCAP 格式，不录原始图像）
ros2 run dozer_tools record_bag.sh
```

环境变量（已写入 `~/.bashrc`）：`ROS_DOMAIN_ID=42`、`RMW_IMPLEMENTATION=rmw_fastrtps_cpp`。

---

## 8. 协作流程

1. **代码以 git 仓库为准**。本目录是 git 仓库，后续推送到团队代码托管平台；Jetson 上的副本用于联调和部署，**不要多人直接在 Jetson 上改代码**。
2. **日常开发在 x86 PC 上**（Ubuntu 22.04 + Humble），用 `dozer_tools` 的模拟数据或共享 bag 回放调试；Jetson 只用于集成验证。
3. **分支**：`main` 受保护；功能分支命名为 `<模块>/<简述>`，例如 `radar/sr73f-driver`；通过合并请求合入。
4. **提交规范**：`<type>(<scope>): <中文摘要>`，type 取值同 rtc-framework 的 `AGENTS.md`；scope 用包名去掉 `dozer_` 前缀，例如 `feat(radar): 增加 SR73F 目标解析`。
5. **接口先行**：涉及跨模块的数据先改 `dozer_msgs` 并评审合入，再改实现。
6. **模拟数据**：上游模块未就绪时，下游使用 `dozer_tools` 的模拟发布器开发；新模块应顺带提供自己的模拟发布器。
7. **共享数据**：实车录制的 bag 放在共享盘，统一命名 `dozer_<日期>_<场景>`，并附简短说明。
8. **局域网隔离**：车上固定使用 `ROS_DOMAIN_ID=42`；开发 PC 请使用其它 ID，避免数据串入车端。

---

## 9. 安全规则

1. 网络控制链路不是经过安全认证的控制通道；本地制动、限位、硬件急停始终拥有最高优先级。
2. 驾驶指令看门狗在 `dozer_vehicle` 内判定（默认 300 ms），未经安全评审不得延长或放宽序号与范围校验。
3. 控制指令只能由 `dozer_rtc_bridge` 发布到 `/vehicle/drive_cmd`；其它模块不得发布控制指令。
4. 涉及 `dozer_vehicle` 和控制相关消息的修改必须附带测试，并经过安全评审。

---

## 10. 硬件与系统现状（2026-10-12）

| 项目 | 现状 |
|---|---|
| CAN | `can0` 为 SoC MTTCAN（规划给雷达）；`can1`/`can2` 为 SPI 外挂 MCP2518FD。开机由 `/opt/nx_port_init/nx_can_init.sh` 以 500 kbps 拉起 |
| 雷达 | SR73F，资料在 `autoDozer/driver/homibo/`；规划 SensorID 0-3、1 Mbps、每台最多输出 30 个目标。雷达改为 1 Mbps 后再修改 `can0` 的 bitrate |
| 相机 | 8 路 GMSL，`/dev/video0-7`，UYVY |
| 已安装 | ROS 2 Humble desktop、`rosbag2-storage-mcap`、`foxglove-bridge`、cantools（pip 用户目录） |
| 系统参数 | `/etc/sysctl.d/60-ros2-dds.conf`：`rmem_max`/`wmem_max` = 32 MB |
| 注意 | 内存仅 8 GB，联调时尽量关闭 VS Code Server 等占用大的进程 |

---

## 变更记录

| 日期 | 内容 |
|---|---|
| 2026-10-12 | 初始化工作空间骨架、`dozer_msgs` 第一版、统一约定 |
