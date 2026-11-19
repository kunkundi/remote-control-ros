# dozer_rtc_bridge

RTC 网关（通信层），由 rtc-framework 的 `rtc_edge` 改造而来。负责人：RTC 模块负责人。

- 视频：从 `dozer_camera_gmsl` 取帧 → NVENC → WebRTC（不经过 DDS）
  - 第一阶段直接链接 `dozer_camera_gmsl` 库；第二阶段两者作为 component 加载到同一进程，通过进程内通信零拷贝传帧
- 发布：`/vehicle/drive_cmd`
- 订阅：`/perception/front_left/detections`、`/perception/front_right/detections`、`/radar/*/objects`、`/vehicle/state`，编码为 nanopb 后经 DataChannel 上传
- 调用：`/vehicle/set_gear` 服务
- 依赖：工作空间包 `dozer_rtc_sdk`，导入目标 `dozer_rtc_sdk::vtsrtc`

不再包含相机采集代码。改造范围与顺序见工作空间 README 第 7 节。
