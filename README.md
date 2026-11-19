# autoDozer 车端 ROS 2 工作空间

本目录是推土机远程驾驶平台车端（Jetson Orin NX）的 ROS 2 工作空间，负责传感器采集、感知算法、结果上传与远程控制下行。
本 README 是各模块开发的**统一约定**，新增或改造模块前请先通读；对约定的修改需要评审（见“协作流程”）。

- 平台：Jetson Orin NX 8GB，JetPack 6（L4T R36.4），Ubuntu 22.04
- ROS：Humble，RMW 为 Fast DDS（`rmw_fastrtps_cpp`）
- 语言：C++17（ROS 节点），Python 3.10（工具与 launch）

---

## 1. 整体架构

```text
/dev/video0..5 -> dozer_rtc_edge: V4L2 -> CUDA I420 -> 640x360
                          |-> I420 stereo/grid -> NVENC -> RTC console
                          |-> per-camera latest-frame shared memory (6 cameras)
                                       |-> dozer_perception (YOLO)
                                       |        -> ROS detections
                                       |-> detection_viewer
dozer_radar_sr73f -> ROS radar objects -> dozer_fusion -> ROS fused detections
dozer_audio -> ROS PCM -> dozer_rtc_edge -> RTC console
```

当前采用方案二：edge 直采，图像像素经共享内存送给感知进程，不经过 ROS 图像话题，也不从 RTC 编码视频回解。六路各 640x360、目标 30 FPS，双目 1280x360、环视网格 640x360。转换、缩放和共享快照仍有拷贝，不是零拷贝；实机端到端帧率尚需验收。edge 退出会中断感知输入，普通网络断连或视图切换不关闭采集。
`dozer_vehicle`、`dozer_rtc_bridge` 仍为后续规划；当前控制保留 edge 内 debug 模式和 300 ms 看门狗，不能视为已接入实车。后文迁移阶段规划不代表这些功能已经完成。

分层：

| 层 | 职责 | 包 |
|---|---|---|
| 接口层 `interfaces/` | 消息与服务定义 | `dozer_msgs` |
| 驱动层 `drivers/` | 读硬件、解码、打时间戳、发布原始数据 | `dozer_audio`、`dozer_camera_gmsl`、`dozer_radar_sr73f`，以后的激光雷达/IMU/GNSS/底盘 CAN 等 |
| 感知层 `perception/` | 检测、跟踪、融合等算法 | `dozer_perception`、`dozer_fusion` |
| 控制层 `control/` | 车辆控制与安全看门狗 | `dozer_vehicle` |
| 通信层 `comm/` | 与远程驾驶台的通信 | `dozer_rtc_bridge` |
| 远程驾驶 `remote_drive/` | RTC 车端和独立操作台 | `dozer_rtc_edge`、`dozer_rtc_console` |
| 系统层 `system/` | 启动编排、配置、工具 | `dozer_bringup`、`dozer_tools` |

驱动层不依赖感知/控制/通信层；感知层通过共享内存图像协议或 ROS 消息使用传感器数据，不链接驱动代码。当前相机采集库在 edge 内运行，ROS 相机节点仅保留为调试回退。

设计原则：

1. **图像不走 DDS**：RTC 直接接收进程内 I420，独立算法通过有界共享快照取最新帧；慢消费者不得阻塞采集。
2. **结果走 ROS，对外沿用 nanopb**：检测、雷达、融合仍用 ROS 消息；检测结果上传适配尚未完成。
3. **控制独立是后续目标**：真实车辆适配与独立看门狗需审查后实现，当前仅保留 edge 的调试控制。
4. **感知进程隔离**：算法退出不关闭 edge；edge 退出时消费者拒绝过期帧，预览标明断流。

---

## 2. 目录结构

```text
ros2_ws/
    ├── README.md
    ├── vendor/rtc-framework/       # RTC、Jetson 编码器、相机、信令服务源码
    ├── vendor/jetson/              # 固定版本的第三方 Jetson 开发包
    └── src/
        ├── interfaces/
        │   └── dozer_msgs/         # 接口定义（msg/srv），修改需全员评审
        ├── drivers/
        │   ├── dozer_media_vendor/ # 导出多媒体依赖
        │   ├── dozer_audio/        # PCM 音频采集节点与播放驱动库
        │   ├── dozer_camera_gmsl/  # GMSL 相机（8 路 V4L2）
        │   └── dozer_radar_sr73f/  # SR73F 毫米波雷达（SocketCAN）
        ├── perception/
        │   ├── dozer_perception/   # 感知算法（YOLO/TensorRT 等）
        │   └── dozer_fusion/       # 多传感器融合（后期）
        ├── control/
        │   └── dozer_vehicle/      # 车辆控制与安全看门狗
        ├── comm/
        │   └── dozer_rtc_bridge/   # RTC 网关（由 rtc_edge 改造而来）
        ├── remote_drive/
        │   ├── dozer_rtc_sdk/      # 从源码编译并导出 libvtsrtc，同时构建信令服务
        │   ├── dozer_rtc_edge/     # 边缘端直采、RTC 视频、共享内存图像输出
        │   └── dozer_rtc_console/  # 独立操作台：视频、控制、音频与检测叠框
        └── system/
            ├── dozer_bringup/      # launch、config/*.yaml、传感器外参、systemd
            └── dozer_tools/        # 模拟数据发布器、录包脚本等开发工具
```

分类目录只用于组织，colcon 会递归查找包；包名全局唯一，以 `dozer_` 开头。

`dozer_rtc_edge` 默认独占六路设备，编译仓库内的 V4L2 采集和 CUDA 转换源码；不要同时启动 ROS 相机驱动。每路输出 `/dev/shm/dozer_camera_<name>`，名称为 front_left、front_right、front、rear、left、right，生产者与消费者需同一用户。根脚本默认为六路相机启动 YOLO。详细协议及复制边界见 edge 文档。
音频由 `dozer_audio` 的 `audio_capture_node` 独占采集，发布 `/audio/capture`，edge 通过订阅接入 RTC，不再打开麦克风。使用 `ros2 launch dozer_bringup audio.launch.py` 或总启动入口的 `use_audio:=true` 启动；设备参数见 `dozer_bringup/config/audio.yaml`。远端音频播放仍由 edge 调用驱动库。
运行配置集中在 `dozer_bringup/config/rtc_edge/`，构建、依赖与当前限制见 [src/remote_drive/dozer_rtc_edge/README.md](src/remote_drive/dozer_rtc_edge/README.md)。
它尚未完成 ROS 检测结果上传与 `/vehicle/*` 翻译，不替代规划中的 `dozer_rtc_bridge`。

`dozer_rtc_console` 可以单独构建和运行。发送端和操作台都链接 `dozer_rtc_sdk::vtsrtc`，并在各自的可执行文件旁安装同一份动态库。
配置集中在 `dozer_bringup/config/rtc_console/`。构建和资产交付要求见
[src/remote_drive/dozer_rtc_console/README.md](src/remote_drive/dozer_rtc_console/README.md)。

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

| 包 | 层 | 职责 | 输入 | 输出 | 负责人 | 状态 |
|---|---|---|---|---|---|---|
| `dozer_msgs` | 接口 | 接口定义 | — | — | 架构负责人 | 第一版已提交 |
| `dozer_audio` | 驱动 | PCM 采集节点、SDL3 设备枚举与播放库 | 音频设备、PCM 帧 | `/audio/capture`（100 Hz）、声卡播放 | 待定 | 独立采集节点与 dummy 测试通过，待实机验证 |
| `dozer_camera_gmsl` | 驱动 | 六路 V4L2 ROS 回退采集、降采样与时间戳 | 配置的 `/dev/videoN` | `/camera/<name>/image_small` | 待定 | 默认不启动；不能与 edge 直采同时占用设备 |
| `dozer_radar_sr73f` | 驱动 | SR73F 驱动 + 配置工具 `sr73f_config`，can0，SensorID 0-3 | can0 | `/radar/<name>/objects`、`/radar/<name>/points`、`/radar/<name>/status` | 待定 | 已实现，待硬件验证 |
| `dozer_perception` | 感知 | 检测/感知算法 | edge I420 共享内存（ROS topic 可显式回退） | `/perception/<name>/detections` | 待定 | 可选 1～6 路，根脚本默认六路 |
| `dozer_fusion` | 感知 | 雷达 + 视觉融合 | 雷达、检测、tf | `/fusion/objects` | 待定 | 后期 |
| `dozer_vehicle` | 控制 | 驾驶指令看门狗、换档、车辆接口 | `/vehicle/drive_cmd`、`/vehicle/set_gear` | `/vehicle/state` | 待定 | 骨架 |
| `dozer_rtc_bridge` | 通信 | 视频推流、结果上传、控制下行 | 相机帧、上述 topic、DataChannel | 视频、DataChannel、`/vehicle/*` | RTC 模块负责人 | 骨架 |
| `dozer_rtc_console` | 远程驾驶 | 独立操作台、视频显示、控制与检测叠框 | RTC 视频、音频、DataChannel | RTC 控制与视图请求 | 待定 | 已迁入，构建与无硬件测试通过，待实际联调 |
| `dozer_bringup` | 系统 | 启动编排、参数、外参 | — | `/tf_static` | 集成负责人 | 骨架 |
| `dozer_tools` | 系统 | 模拟数据、录包 | — | — | 集成负责人 | 可用 |

负责人确定后填写到表中和各包的 `package.xml`（`<maintainer>`）中。

### 3.1 一键启动

在工作空间根目录执行：

```bash
./start_all.sh
```

脚本先使用 colcon 构建所需包，再通过同一个 ROS launch 启动直采六路相机的 RTC edge、SR73F 雷达、
音频采集、六路共享内存 YOLO 检测、三台雷达、雷达视觉融合和静态 TF，不启动 ROS 相机节点或预览窗口。按 Ctrl+C 统一停止。

```bash
./start_all.sh --keep              # 保留已运行模块，启动缺失模块（默认）
./start_all.sh --restart           # 重启当前工作空间中已运行的模块
./start_all.sh --no-build          # 已构建过时跳过编译
./start_all.sh --viewer            # 打开六路检测与前左雷达窗口
./start_all.sh --headless          # 不打开预览窗口（默认）
./start_all.sh --dry-run           # 仅打印构建和启动命令，不访问硬件
```

`--perception-cameras` 接受不重复的逗号分隔相机名单，默认六路，不适用于 `--edge-only`。共享内存模式选择一至六路时均使用单个固定 batch=6 的 INT8 TensorRT YOLO 进程；不足六路时填充输入，但仅发布实际更新的相机结果，因此少于六路仍会计算六张图。只处理最新帧，不排队积压。六路批推理不能保证每路 30 FPS。预览可显示所选相机；雷达窗口只显示前左雷达。

从旧 ROS 图像链路切换时执行 `./start_all.sh --restart`；`--keep` 发现旧相机节点或无共享内存输出的旧 edge 会拒绝启动。检查新命令而不停止服务可用 `./start_all.sh --restart --dry-run`。

只需要 RTC edge、六路相机和音频采集时，使用 [start_edge.sh](start_edge.sh)：

```bash
./start_edge.sh                    # 构建并启动直采 edge 和音频
./start_edge.sh --restart --no-build # 跳过编译，显式切换或重启
./start_edge.sh --dry-run          # 仅打印命令，不访问硬件
```

该脚本不启动雷达、感知、融合、静态 TF 或预览，不要求 `.venv-gpu` 与 YOLO 模型；
复用总启动脚本的 `--edge-only` 模式，保留和重启操作仅作用于旧 camera、audio、edge，
不会停止其他已运行模块。不要与其他启动脚本并行执行。

首次使用前安装 ROS 2 Humble 和工程依赖，并通过 `./setup_gpu_inference.sh` 准备
`.venv-gpu` 与 YOLO 模型。相机、CAN 和音频设备需已就绪且当前用户有访问权限；
脚本不会自动安装依赖、配置 CAN 接口或修改车辆安全设置。参数继续集中在
`src/system/dozer_bringup/config/`，RTC 使用其中 `rtc_edge/rtc.cfg` 与 `rtc_edge/edge.cfg`。
`--keep` 和 `--restart` 无需模块名、不可同时使用，可与 `--no-build`、`--headless` 组合。
脚本按当前用户和本工作空间安装路径识别模块进程；静态 TF 需由本工作空间的
`dozer_bringup` launch 启动。保留已有进程时，Ctrl+C 只停止本次新启动的模块，
原有进程仍由原启动终端管理；进程存在不代表话题、硬件或网络健康，TF 完整性需单独检查。
检测进程的相机名单、数量与请求不一致时，保留模式会报错，使用 `--restart` 切换或恢复；所选相机缺少共享内存出口的旧 edge 也不能保留。
重启模式先发送 SIGINT 并等待退出；15 秒内未退出或被外部管理器重新拉起时，
脚本报错退出，不强制杀进程或启动重复实例。`--headless` 不新开预览；
保留模式不关闭旧预览，重启模式会停止旧预览。
不要并行启动多个启动脚本，或同时使用其他工作空间的相机采集程序，以免重复占用设备。
`dozer_vehicle` 与旧 `dozer_rtc_bridge` 尚为骨架，不在启动范围内；
RTC 的车辆控制仍受现有 debug 模式约束，不代表已接入真实车辆执行。

总 launch 新增 `use_rtc_edge:=true`，默认关闭；可通过 `rtc_config:=...` 覆盖 RTC 配置路径。
RTC 仅接受 `--config`，运行参数由 `rtc.cfg` 的 `edge` 段或其 `config` 引用文件加载，
不支持 `--edge-config`。默认 `image_backend:=shm`；真实/模拟 ROS 相机回退需 `image_backend:=ros`，且 edge 的设备字段应改为相应 ROS topic，不能与直采同时占用设备。音频启动方式不变。

### 3.2 六路感知配置

当前设备映射如下，物理安装方向仍需现场核对：

| 设备 | 相机名称 | 默认 YOLO |
|---|---|---|
| `/dev/video0` | `front_left` | 启用 |
| `/dev/video1` | `front_right` | 启用 |
| `/dev/video2` | `front` | 关闭 |
| `/dev/video3` | `rear` | 关闭 |
| `/dev/video4` | `left` | 关闭 |
| `/dev/video5` | `right` | 关闭 |

六路采集和共享快照不依赖是否启动对应 YOLO。每路共享文件为 `/dev/shm/dozer_camera_<name>`，检测输出为 `/perception/<name>/detections`；RTC 环视通道的 `surround_` 前缀不用于感知相机名称。每路模型、阈值和设备参数见 [src/system/dozer_bringup/config/perception.yaml](src/system/dozer_bringup/config/perception.yaml)。

先预检查六路启动命令，不构建、不停止服务、不打开设备：

```bash
./start_all.sh --restart --dry-run \
  --perception-cameras front_left,front_right,front,rear,left,right
```

确认可以中断当前服务后，再执行实际启动；`--restart` 会停止并重新启动脚本管理的相关模块：

```bash
./start_all.sh --restart \
  --perception-cameras front_left,front_right,front,rear,left,right
```

相机名单不能包含空项、空格、重复项或未知名称。可将名单改为 `rear` 等非空子集，仅改变 YOLO 输入选择，不关闭其他相机采集，也不改变雷达、融合或预览的启动范围。直接使用 launch 时传入 `use_perception:=true perception_cameras:=...`，并确保同一用户的直采 edge 已运行。

### 3.3 无硬件回归

启用 `BUILD_TESTING=ON` 并完成构建后，在工作空间根目录运行：

```bash
source install/setup.bash
ROS_DOMAIN_ID=182 ROS_LOCALHOST_ONLY=1 \
  DOZER_SHM_TEST_WRITER="$PWD/build/dozer_rtc_edge/shared_camera_frame_tests" \
  colcon test --packages-select dozer_rtc_edge dozer_perception dozer_bringup \
  --return-code-on-test-failure
```

当前 16 项 CTest 覆盖图像接入、共享内存隔离与过期处理、跨语言消费，以及相机名单和启动保护；其中感知与 bringup 测试分别包含 6 项、8 项 Python 用例。这些测试不打开真实相机、不重启运行服务，也不代表六路 GPU 推理、RTC 接收帧率或实车融合精度已经验收。

### 3.4 独立 RTC Console

console 不随 `start_all.sh` 或 `start_edge.sh` 启动。在工作空间根目录单独执行：

```bash
source /opt/ros/humble/setup.bash
colcon build --packages-up-to dozer_rtc_console --cmake-args -DCMAKE_BUILD_TYPE=Release
source install/setup.bash
ros2 run dozer_rtc_console rtc_console
```

使用源码中的集中配置，避免修改安装副本：

```bash
ros2 run dozer_rtc_console rtc_console \
  --config "$PWD/src/system/dozer_bringup/config/rtc_console/rtc.cfg"
```

默认不自动打开房间，需在操作台中操作；console 和 edge 的信令服务、房间与认证设置需相互匹配。
本模块没有新增 ROS 订阅，也未实现 `/fusion/objects` 上传；外部依赖、无窗口模式及测试命令见模块文档。

---

## 4. 接口约定

### 4.1 Topic

| Topic | 类型 | 发布者 | QoS | 频率 |
|---|---|---|---|---|
| `/camera/<name>/image_raw` | `sensor_msgs/Image` | dozer_camera_gmsl | SensorData | 采集帧率；仅进程内或调试使用（第二阶段） |
| `/camera/<name>/image_small` | `sensor_msgs/Image` | dozer_camera_gmsl（仅显式回退，默认无图像 topic） | SensorData | 目标 30 Hz |
| `/radar/<name>/objects` | `dozer_msgs/RadarObjectArray` | dozer_radar_sr73f | SensorData | 30 Hz |
| `/radar/<name>/points` | `sensor_msgs/PointCloud2` | dozer_radar_sr73f | SensorData | 30 Hz，仅有订阅者时发布（可视化用） |
| `/radar/<name>/status` | `dozer_msgs/RadarStatus` | dozer_radar_sr73f | Reliable | 1 Hz |
| `/perception/<name>/detections` | `dozer_msgs/Detection2DArray` | dozer_perception | Reliable | 跟随输入 |
| `/fusion/objects` | 待定 | dozer_fusion | SensorData | — |
| `/vehicle/drive_cmd` | `dozer_msgs/DriveCommand` | dozer_rtc_bridge | BestEffort, KeepLast(1) | 跟随远端 |
| `/vehicle/set_gear` | `dozer_msgs/srv/SetGear`（服务） | dozer_vehicle 提供 | — | 事件 |
| `/vehicle/state` | `dozer_msgs/VehicleState` | dozer_vehicle | Reliable | 10 Hz |
| `/diagnostics` | `diagnostic_msgs/DiagnosticArray` | 全部模块 | 默认 | 1 Hz |

命名规则：`/<传感器或模块>/<实例>/<数据>`，实例名用小写加下划线（如 `front_left`、`surround_front`）。

### 4.2 时间戳

- `header.stamp` 填**数据采集时刻**，不是发布时刻。
  - 相机：当前为软件出队时间映射到系统时钟，不是硬件曝光时间；
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

## 5. 驱动层规范

### 5.1 通用规则

1. **一个设备型号一个包**，命名 `dozer_<传感器类型>_<型号或接口>`，例如 `dozer_radar_sr73f`、`dozer_camera_gmsl`、`dozer_imu_xxx`。换型号时新增驱动包，下游只依赖消息，不需要修改。
2. **只做采集**：读硬件、解码协议、打时间戳、发布。滤波、跟踪、融合等算法放在感知层。
3. **优先使用标准消息**：图像 `sensor_msgs/Image`、IMU `sensor_msgs/Imu`、GNSS `sensor_msgs/NavSatFix`、点云 `sensor_msgs/PointCloud2`；标准消息无法表达时（如雷达目标列表）才在 `dozer_msgs` 中新增。
4. **硬件读写与协议解析分离**：解析写成与 ROS、硬件都无关的类，用固定字节/固定帧做 gtest；硬件 I/O 是一层薄封装。没有硬件也能开发和测试。
5. **统一要求**：`header.stamp` 为采集时刻、`frame_id` 为 `<类型>_<实例>`、外参登记在 `extrinsics.yaml`、参数放在 `dozer_bringup/config/<驱动>.yaml`、发布 `/diagnostics`（在线状态、帧率、错误计数）。
6. **设备配置与运行分离**：会写入设备的操作（如雷达改 ID、改波特率）做成单独的命令行工具，驱动节点启动时不自动下发。
7. **每个驱动提供模拟发布器或录包样例**，放在 `dozer_tools`，便于下游开发。

### 5.2 新增传感器流程

1. 在 `drivers/` 下新建 `dozer_<类型>_<型号>` 包（复制现有骨架），README 写明硬件接口、输出 topic、参数。
2. 如需新消息，先在 `dozer_msgs` 中评审合入。
3. 在 `dozer_bringup/config/` 增加参数文件，在 `extrinsics.yaml` 登记坐标系，在 `dozer.launch.py` 接入。
4. 在 `dozer_tools` 提供模拟发布器，在 `record_bag.sh` 的录制规则中补充 topic。
5. 更新本 README 第 3、4 节的表格。

### 5.3 相机的特殊处理

相机数据量大（8 路，单帧 4K UYVY 约 16 MB），不能像其它传感器一样直接跨进程发布原始帧：

| 阶段 | 形式 | 说明 |
|---|---|---|
| 第一阶段 | `dozer_camera_gmsl` 为 C++ 共享库 | 从 rtc-framework `client/modules/camera/` 迁移；`dozer_rtc_bridge` 直接链接，行为与现有 `rtc_edge` 一致，先理清代码归属 |
| 第二阶段 | `dozer_camera_gmsl` 为 ROS 2 component | 与 `dozer_rtc_bridge` 加载到同一个 `component_container_mt`，开启 `use_intra_process_comms`；帧类型用 `rclcpp::TypeAdapter`（REP-2007）适配 `sensor_msgs/Image`：同进程订阅者拿到共享 CUDA/DMA 缓冲区（零拷贝），只有 rosbag/rviz 等跨进程订阅者出现时才转换 |

降采样帧 `/camera/<name>/image_small` 由 `dozer_camera_gmsl` 发布，供感知算法跨进程订阅。
当前已实现独立的 V4L2 ROS 节点，只发布该降采样帧；上表中的共享库与 component
是原定的 RTC 集成规划，尚未实现。

---

## 6. 包规范

1. ROS 节点使用 C++17、`ament_cmake`；工具脚本可用 Python。
2. 每个节点一个可执行文件，入口为 `src/<名称>_node.cpp`；需要与其它节点同进程运行的（相机、rtc_bridge）写成 component。核心逻辑写成与 ROS 无关的类，便于单元测试。
3. 参数全部通过 ROS 参数声明，默认值写在 `dozer_bringup/config/*.yaml`，代码中不写死设备名、IP 和阈值。
4. 每个模块发布 `/diagnostics`，至少包含在线状态、输入频率和错误计数。
5. 解析外部数据（CAN 报文、DataChannel 载荷）必须做长度、范围和有限值校验，并有对应的 gtest 测试。
6. 代码注释使用中文，仅为不明显的行为、协议约束和安全要求添加注释。
7. 默认模型权重与两种 INT8 引擎随仓库交付。构建、日志、校准采集和其他导出产物由 `.gitignore` 排除。

---

## 7. RTC 源码与单仓库构建

RTC 实现位于 `vendor/rtc-framework/`，从提交 `673cdc0` 导入。修改 C ABI 时同步维护 `vtsrtc/src/c_rtc.h` 与实现。编码器、采集、RTC 应用和协议修改都在本仓库提交。ROS 运行入口分别位于 `src/remote_drive/dozer_rtc_edge` 和 `dozer_rtc_console`。

在 Jetson Orin、JetPack 6（CUDA 12.6、Jetson Multimedia API、TensorRT 10.3）与 ROS 2 Humble 环境，安装 OpenCV、OpenGL、OpenSSL、V4L2、GStreamer 开发包以及 protobuf 编译器。CMake 从仓库解包固定版本的 WebRTC 与媒体依赖，源码可以检出到任意目录。GPU 推理的 Python 环境由 `./setup_gpu_inference.sh` 建立，首次安装 Python 软件包需要网络。依赖版本见 [Jetson 依赖清单](vendor/jetson/README.md)。

```bash
source /opt/ros/humble/setup.bash
export PATH=/usr/local/cuda/bin:$PATH
MAKEFLAGS=-j1 CMAKE_BUILD_PARALLEL_LEVEL=1 colcon build --executor sequential --cmake-args -DCMAKE_BUILD_TYPE=Release
source install/setup.bash
ros2 run dozer_rtc_console rtc_console --help
```

需要自建信令服务时，在含有 `signaling-server.cfg` 和 `web/` 的目录启动。仓库附带的配置与静态页面安装在 SDK 的 share 目录。

```bash
cd "$(ros2 pkg prefix --share dozer_rtc_sdk)"
ros2 run dozer_rtc_sdk signaling-server
```

两个客户端的 `rtc.cfg` 可分别配置为实际信令服务地址。

---

## 8. 编译与运行

```bash
cd ~/workspace/ros2_ws
colcon build --symlink-install
source install/setup.bash        # ~/.bashrc 中已配置自动 source

# 只编译某个包（及其依赖）
colcon build --packages-up-to dozer_rtc_bridge

# 启动外参发布 + 雷达驱动（use_mock_radar:=true 则用模拟雷达）
ros2 launch dozer_bringup dozer.launch.py use_radar:=true
ros2 topic hz /radar/front_left/objects

# 单独启动雷达驱动 / 回放 candump 日志
ros2 launch dozer_bringup radar.launch.py
ros2 launch dozer_bringup radar.launch.py replay_file:=/path/to/candump.log

# 录包（MCAP 格式，不录原始图像）
ros2 run dozer_tools record_bag.sh
```

环境变量（已写入 `~/.bashrc`）：`ROS_DOMAIN_ID=42`、`RMW_IMPLEMENTATION=rmw_fastrtps_cpp`。

---

## 9. 协作流程

1. **代码以 git 仓库为准**。本目录是 git 仓库，后续推送到团队代码托管平台；Jetson 上的副本用于联调和部署，**不要多人直接在 Jetson 上改代码**。
2. **日常开发在 x86 PC 上**（Ubuntu 22.04 + Humble），用 `dozer_tools` 的模拟数据或共享 bag 回放调试；Jetson 只用于集成验证。
3. **分支**：`main` 受保护；功能分支命名为 `<模块>/<简述>`，例如 `radar/sr73f-driver`；通过合并请求合入。
4. **提交规范**：`<type>(<scope>): <中文摘要>`，type 取值同 rtc-framework 的 `AGENTS.md`；scope 用包名去掉 `dozer_` 前缀，例如 `feat(radar_sr73f): 增加目标解析`。
5. **接口先行**：涉及跨模块的数据先改 `dozer_msgs` 并评审合入，再改实现。
6. **模拟数据**：上游模块未就绪时，下游使用 `dozer_tools` 的模拟发布器开发；新模块应顺带提供自己的模拟发布器。
7. **共享数据**：实车录制的 bag 放在共享盘，统一命名 `dozer_<日期>_<场景>`，并附简短说明。
8. **局域网隔离**：车上固定使用 `ROS_DOMAIN_ID=42`；开发 PC 请使用其它 ID，避免数据串入车端。

---

## 10. 安全规则

1. 网络控制链路不是经过安全认证的控制通道；本地制动、限位、硬件急停始终拥有最高优先级。
2. 驾驶指令看门狗在 `dozer_vehicle` 内判定（默认 300 ms），未经安全评审不得延长或放宽序号与范围校验。
3. 控制指令只能由 `dozer_rtc_bridge` 发布到 `/vehicle/drive_cmd`；其它模块不得发布控制指令。
4. 涉及 `dozer_vehicle` 和控制相关消息的修改必须附带测试，并经过安全评审。

---

## 11. 硬件与系统现状（2026-10-12）

| 项目 | 现状 |
|---|---|
| CAN | `can0` 为 SoC MTTCAN（规划给雷达）；`can1`/`can2` 为 SPI 外挂 MCP2518FD。开机由 `/opt/nx_port_init/nx_can_init.sh` 以 500 kbps 拉起 |
| SocketCAN | 内核 `can`、`can_raw` 为可加载模块，已通过 `/etc/modules-load.d/can.conf` 开机自动加载 |
| 雷达 | SR73F，资料在 `autoDozer/driver/homibo/`；规划 SensorID 0-3、1 Mbps、每台最多输出 30 个目标。雷达改为 1 Mbps 后再修改 `can0` 的 bitrate |
| 相机 | 8 路 GMSL，`/dev/video0-7`，UYVY；当前双目 video0/1，环视 video2-5 |
| 已安装 | ROS 2 Humble desktop、`rosbag2-storage-mcap`、`foxglove-bridge`、cantools（pip 用户目录） |
| 系统参数 | `/etc/sysctl.d/60-ros2-dds.conf`：`rmem_max`/`wmem_max` = 32 MB |
| **禁止** | **不要在本机执行 `apt upgrade`**：载板使用厂商定制内核，升级 `nvidia-l4t-*` 会覆盖内核、模块和 bootloader |
| 内核 | 厂商内核（`03-OrinNxNano-DC-GMSL-8CH-image_jp6-2-1_2v1`，Jul 3 2026）。2026-10-11 被 `apt-get upgrade` 覆盖后已于 10-13 回滚；`nvidia-l4t-*` 已 `apt-mark hold`；备份与回退脚本见 `~/backup_kernel_36.4.7_20261013/`、`~/rollback_to_stock.sh` |
| 注意 | 内存仅 8 GB，联调时尽量关闭 VS Code Server 等占用大的进程 |

---

## 变更记录

| 日期 | 内容 |
|---|---|
| 2026-10-12 | 初始化工作空间骨架、`dozer_msgs` 第一版、统一约定 |
| 2026-10-12 | 按层组织目录；新增驱动层规范与 `dozer_camera_gmsl`；`dozer_radar` 改名为 `dozer_radar_sr73f` |
| 2026-10-12 | 实现 `dozer_radar_sr73f` 驱动与 `sr73f_config` 工具；`RadarStatus` 增加固件、周期频率、区域字段；新增 `radar.launch.py` |
