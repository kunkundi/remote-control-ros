# dozer_rtc_console

从 rtc-framework 迁入的独立 RTC 操作台，使用 C++17、ament_cmake 和 colcon 构建。
应用源码位于 `src/app/`，回归测试位于 `test/`，不链接 `dozer_rtc_edge`，不加入车端默认启动流程。
本包编译和运行均不再依赖相邻的 rtc-framework 仓库。

保留上游 SDL3 / ImGui / OpenGL 界面、车辆槽位、控制发送、视图切换、音频、检测叠框和本地 YOLO。
它直接使用 RTC C SDK，不是 ROS 节点，也不订阅 `/fusion/objects`。此次迁移不包含 ROS 融合结果上传或新协议实现。

## 依赖

- ROS 2 Humble、ament_cmake、C++17 编译器、OpenGL 开发库。
- 本工作空间的 `dozer_audio` 播放/采集库及其 SDL3 依赖。
- 包内 `vendor/vtsrtc/` 的匹配 C SDK 头文件和动态库。
- nanopb、nlohmann_json、libyuv、ONNX Runtime、含 SDL3 和 OpenGL3 后端的 ImGui。

默认复用本机已有 xmake 依赖缓存，不下载依赖、不调用上游 xmake 构建，也不修改上游仓库。
runtime、vision 和 camera 所需公共头文件位于 `include/`，实现位于 `src/runtime/`、`src/vision/`，
车辆和视觉协议的定义、生成文件、头文件与编解码实现位于 `protocols/`。这些文件均由本包维护，
不通过 edge 或外部仓库间接包含。

当前机器已迁入以下本地发行资产：

```text
vendor/vtsrtc/include/c_rtc.h
vendor/vtsrtc/lib/libvtsrtc.aarch64.so
models/yolo26n.onnx
```

SDK 是匹配原头文件的预编译库，不是 SDK 实现源码；不需要先在其他仓库编译或安装 SDK。
库随本包安装到可执行文件同目录，程序以库名链接，并通过 `$ORIGIN` 加载，避免写入原仓库绝对路径。
当前库面向 Jetson/aarch64，仍依赖系统 GStreamer、V4L2 和 JetPack/NVIDIA 运行库，不是通用 x86 SDK。

SDK 库和模型被 Git 忽略，不作为普通源码提交；仅克隆本仓库不会自动得到这些二进制资产。
迁移到其他机器时，需由独立 SDK/模型发行包提供上述文件，或通过下列变量指定已有文件；
无需提供原 rtc-framework 仓库。JSON、SDL3、ImGui、ONNX Runtime 等第三方依赖仍需单独安装。

可通过 `--cmake-args` 覆盖这些 CMake 缓存变量：

| 变量 | 默认值 | 用途 |
|---|---|---|
| `VTSRTC_SDK_ROOT` | 本包的 `vendor/vtsrtc` | 独立 SDK，包含 `include/` 与 `lib/` |
| `RTC_DEPENDENCY_CACHE` | `$HOME/.xmake/packages` | 依赖前缀搜索路径 |
| `CONSOLE_YOLO_MODEL` | 本包的 `models/yolo26n.onnx` | 存在时安装的可选本地推理模型 |

也可通过 `CMAKE_PREFIX_PATH`、`imgui_DIR` 和对应的库/头文件缓存变量指定已有依赖。
SDL3 的查找由 `dozer_audio` 管理。SDK 头文件和库只能从选定 SDK 目录查找，不回退到上游安装目录。
原 `RTC_FRAMEWORK_ROOT`、`RTC_FRAMEWORK_INSTALL` 和 `yolo_model` 参数已不再使用。
启用 console 本地推理时必须提供可用模型；是否安装模型不影响 GUI 程序的编译。

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
仍会初始化 RTC、推理及相关音频功能。仅查看入口请使用 `--help`。
`--room` 覆盖房间名称，不覆盖默认的手动开房设置。

## 配置

集中配置位于 `src/system/dozer_bringup/config/rtc_console/`：

- `rtc.cfg`：信令、网络、认证和共享 YOLO 模式，通过 `console.config` 引用同目录的 `console.cfg`。
- `console.cfg`：房间、设备槽位、推理和方向盘设置；未指定的按键使用上游默认值。

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
模型存在时额外执行本地 YOLO 合成图像推理测试，共 11 项 CTest；模型不存在时为 10 项。
这些测试不启动 RTC 会话、不打开真实相机，不验证 GUI 显示、真实音频设备、RTC 网络链路或实车控制。

当前本地 SDK 没有新的视频延迟统计接口。CMake 同时检查头文件与链接符号，支持时启用该统计；
不支持时仅显示统计不可用，不替换 SDK 头文件、不伪造 ABI，也不影响其他功能的编译。
console 和 edge 的车辆、视觉 schema 已比对一致；协议变更仍需两端同步验证。