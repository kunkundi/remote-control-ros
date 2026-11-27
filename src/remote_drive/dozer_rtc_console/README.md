# dozer_rtc_console

从 rtc-framework 迁入的独立 RTC 操作台，使用 C++17。Jetson 上使用 ament_cmake / colcon，Windows x64 上使用独立 CMake / MSVC 构建。
应用源码位于 `src/app/`，回归测试位于 `test/`，不链接 `dozer_rtc_edge`，不加入车端默认启动流程。
本包编译和运行均不再依赖相邻的 rtc-framework 仓库。

保留上游 SDL3 / ImGui / OpenGL 界面、车辆槽位、控制发送、视图切换、音频和检测叠框。
console 不是 ROS 节点，不运行本地推理；它通过 `perception.fused.v1` 接收 edge 转发的 ROS 融合结果。

## 依赖

在 Jetson Orin 上使用 ROS 2 Humble、JetPack 6、OpenGL、protobuf 和本工作空间的 `dozer_audio`、`dozer_media_vendor`、`dozer_rtc_sdk`、`dozer_rtc_common`。colcon 从同仓库源码构建 vtsrtc，操作台通过 `$ORIGIN` 加载安装在可执行文件旁的动态库。`dozer_rtc_common` 提供与车端共用的车辆、视图切换和融合检测协议。

## Windows 构建与运行

支持 Windows 10（1903 或更新）/11 x64、Visual Studio 2022（安装“使用 C++ 的桌面开发”和 Windows SDK）、CMake 3.24 或更高版本。
Windows 不需要 ROS、ament、colcon 或 Jetson 开发环境。编译需要 CUDA Toolkit（验证版本 12.6）和 NVIDIA Video Codec SDK 11.0.10。RTC SDK、音频和共享协议均从本仓库源码编译。

先按 [Windows 开发包说明](../../../vendor/windows/README.md) 准备匹配的 WebRTC 和 Video Codec SDK。
默认位置为 `vendor/windows/webrtc-win/` 和 `vendor/windows/Video_Codec_SDK_11.0.10/`，也可以指定其它目录。
不能使用 Jetson 的 `.a` 库或任意版本的 WebRTC：头文件和 `webrtc.lib` 必须匹配本项目使用的接口与 ABI。

在工作空间根目录的 PowerShell 中执行：

```powershell
.\src\remote_drive\dozer_rtc_console\build_windows.ps1
# 开发包在其它位置时：
.\src\remote_drive\dozer_rtc_console\build_windows.ps1 -WebRtcRoot D:\deps\webrtc-win -NvCodecRoot D:\deps\Video_Codec_SDK_11.0.10
.\install\windows-console\bin\rtc_console.exe --help
.\install\windows-console\bin\rtc_console.exe
```

脚本默认编译 Release、运行测试并安装到 `install/windows-console/bin/`。可以传入
`-Configuration Debug` 或 `-Configuration RelWithDebInfo`、`-BuildDirectory`、`-InstallDirectory`；
`-SkipTests` 仅跳过执行测试。脚本不会自动启动 RTC 会话。

首次 CMake 配置通过 HTTPS 获取固定版本并校验 SHA-256：SDL 3.4.12、ImGui 1.92.6、
nlohmann/json 3.12.0、nanopb 0.4.9、Asio 1.32.0、spdlog 1.14.1、Protobuf 3.21.12。
后续增量构建复用缓存。离线构建可通过 `FETCHCONTENT_SOURCE_DIR_SDL3`、`_IMGUI`、
`_NLOHMANN_JSON`、`_NANOPB`、`_ASIO`、`_SPDLOG`、`_PROTOBUF` 指定各自解压后的源码目录。
Protobuf 编译器随构建生成，不要求预装 Python 或 protoc。

也可直接调用 CMake：

```powershell
cmake -S src/remote_drive/dozer_rtc_console -B build/windows-console `
  -G "Visual Studio 17 2022" -A x64 -DDOZER_WEBRTC_ROOT=D:/deps/webrtc-win `
  -DDOZER_NVCODEC_ROOT=D:/deps/Video_Codec_SDK_11.0.10 -DBUILD_TESTING=ON
cmake --build build/windows-console --config Release --parallel
ctest --test-dir build/windows-console -C Release --output-on-failure
cmake --install build/windows-console --config Release --prefix install/windows-console --component Runtime
```

构建目录中的入口为 `build/windows-console/bin/Release/rtc_console.exe`。
交付时复制整个安装目录（`bin/` 包含 EXE、`vtsrtc.dll`、`rtc.cfg`、`console.cfg`，`share/licenses/` 保存第三方许可证），
目标机需安装 x64 Visual C++ 2015–2022 Redistributable 和支持 H.264 NVDEC 的 NVIDIA 显卡驱动，无需 CUDA Toolkit 或开发机 PATH。配置放在 EXE 旁，也可通过 `--config` 指定。
Windows 默认 `use_NVDEC=true`：接收端协商 H.264，经 NVIDIA NVDEC 解码并转为 I420 交给显示模块。驱动或硬件初始化失败会报错，不会静默切换为软件解码。需要对照排查时，才显式设置 `use_NVDEC=false` 使用 WebRTC 内置解码器。发送端编码仍使用 WebRTC 内置编码器。
中文字体默认使用 Windows 的微软雅黑或黑体。Linux 专用 `readelf` / `ldd` 检查在 Windows 上由安装目录启动检查替代，
并增加 SDK ABI、SDL dummy 音频和 NVDEC 实机测试。可单独运行 `ctest --test-dir build/windows-console -C Release -L hardware --output-on-failure`：测试使用操作台相同的解码工厂，在 GPU 上解码两种分辨率的 H.264 帧并校验 I420 像素和 RTP 时间戳。无需信令服务器、相机或车辆。实际视频帧率、麦克风、方向盘和与车端的网络联调仍需在目标机器验收。

## Jetson 构建与运行

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
构建时将集中配置复制到构建目录，Jetson 安装时复制到 `install/dozer_rtc_console/lib/dozer_rtc_console/`，Windows 安装到 `install/windows-console/bin/`。
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
