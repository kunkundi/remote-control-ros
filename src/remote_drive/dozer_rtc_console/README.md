# dozer_rtc_console

从 rtc-framework 迁入的独立 RTC 操作台，使用 C++17、ament_cmake 和 colcon 构建。
应用源码位于 `src/app/`，回归测试位于 `test/`，不链接 `dozer_rtc_edge`，不加入车端默认启动流程。
本包编译和运行均不再依赖相邻的 rtc-framework 仓库。

保留上游 SDL3 / ImGui / OpenGL 界面、车辆槽位、控制发送、视图切换、音频和检测叠框。
console 不是 ROS 节点，不运行本地推理；它通过 `perception.fused.v1` 接收 edge 转发的 ROS 融合结果。

## 依赖

在 Jetson Orin 上使用 ROS 2 Humble、JetPack 6、OpenGL、protobuf 和本工作空间的 `dozer_audio`、`dozer_media_vendor`、`dozer_rtc_sdk`。colcon 从同仓库源码构建 vtsrtc，操作台通过 `$ORIGIN` 加载安装在可执行文件旁的动态库。共享检测协议由 protobuf 在构建时生成。

## 构建与运行

在工作空间根目录执行：

```bash
source /opt/ros/humble/setup.bash
colcon build --packages-up-to dozer_rtc_console \
  --cmake-clean-cache \
  --cmake-args -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON
source install/setup.bash
ros2 run dozer_rtc_console rtc_console
```

GUI 需要可用的显示服务和 OpenGL 环境。可执行入口为 `rtc_console`，不是 `console`。
从旧版本切换时，上述 `--cmake-clean-cache` 会重新查找依赖，避免沿用旧 SDK/模型路径。
后续增量构建可省略该参数；不需要修改或移动外部仓库。
常用参数：

```bash
ros2 run dozer_rtc_console rtc_console --help
ros2 run dozer_rtc_console rtc_console --room test1
ros2 run dozer_rtc_console rtc_console \
  --config "$PWD/src/system/dozer_bringup/config/rtc_console/rtc.cfg"
ros2 run dozer_rtc_console rtc_console --headless
```

`--headless` / `--no-render` 只关闭窗口和 OpenGL 上下文，不是安全的离线检查：
仍会初始化 RTC 及相关音频功能。仅查看入口请使用 `--help`。
`--room` 覆盖房间名称，不覆盖默认的手动开房设置。

## 配置

集中配置位于 `src/system/dozer_bringup/config/rtc_console/`：

- `rtc.cfg`：信令、网络和认证，通过 `console.config` 引用同目录的 `console.cfg`。
- `console.cfg`：房间、设备槽位和方向盘设置；未指定的按键使用上游默认值。

本次信令与网络配置以现有 edge 配置为基线复制，随后两端各自独立维护；不要把认证内容写入日志。
默认房间为 `test1`，槽位为 `car_1` 至 `car_4`，`auto_open_room=false`，未假定舵机标定值。
这不会改变 edge 的 debug 模式或看门狗设置，也不代表已经接入实车。

不传 `--config` 时，程序固定使用可执行文件旁的 `rtc.cfg`，不搜索当前工作目录，避免误读 edge 配置。
构建时将集中配置复制到构建目录，安装时再复制到 `install/dozer_rtc_console/lib/dozer_rtc_console/`。
修改源码配置后应重新构建，或直接用 `--config` 指向源码配置；GUI 保存设置会写入当前选用的配置文件。

## 验证与限制

构建成功后执行：

```bash
ctest --test-dir build/dozer_rtc_console --output-on-failure
```

提供参数/配置、控制发送、车辆状态日志、视图、合成布局、叠框、全屏状态、帮助入口和 SDK 链接检查。
融合检测测试覆盖 Protobuf 解码、重复和乱序丢弃、快照过期及空快照清理。
这些测试不启动 RTC 会话、不打开真实相机，不验证 GUI 显示、真实音频设备、RTC 网络链路或实车控制。

操作台使用本仓库 C ABI 提供的视频延迟统计接口，ABI 测试随 `dozer_rtc_sdk` 构建。
console 按设备和相机缓存叠框。`camera:<name>` 更新（包括空结果）只影响对应相机，各路独立在接收后 500 ms 过期。`radar_fusion` 周期快照独立保留，不覆盖单相机更新；未收到带范围标记的更新时兼容旧的整包叠框。带相机框的 3D 目标显示融合坐标，纯雷达目标不绘制视频框。叠框不按采集时间戳与视频做精确同步。修复叠框闪烁需同步更新 `dozer_fusion`、edge 和 console；旧版 `vision.detect.v1` 不兼容。