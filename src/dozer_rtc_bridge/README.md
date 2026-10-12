# dozer_rtc_bridge

RTC 网关，由 rtc-framework 的 `rtc_edge` 改造而来。负责人：RTC 模块负责人。

- 进程内：相机采集 → 格式转换 → NVENC → WebRTC 视频（不经过 DDS）
- 发布：`/camera/<name>/image_small`、`/vehicle/drive_cmd`
- 订阅：`/perception/detections`、`/radar/*/objects`、`/vehicle/state`，编码为 nanopb 后经 DataChannel 上传
- 调用：`/vehicle/set_gear` 服务
- 依赖：`~/workspace/rtc-framework/install`（CMake 变量 `RTC_FRAMEWORK_INSTALL`），导入目标 `vtsrtc::vtsrtc`

改造范围与顺序见工作空间 README 第 6 节。
