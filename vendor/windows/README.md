# Windows 操作台开发包

`dozer_rtc_console` 的 Windows 构建使用 x64 WebRTC Release 头文件和静态库；应用和 vtsrtc DLL 从本仓库源码编译。
开发包不参与 colcon 包发现，也不要求检出相邻的 rtc-framework 仓库。

匹配的包为 RTC 框架 `v1.0.0` 发布附件 `webrtc-win.tar.gz`：

```powershell
# 在工作空间根目录执行
New-Item -ItemType Directory -Force build/downloads, vendor/windows | Out-Null
Invoke-WebRequest -Uri https://github.com/kunkundi/rtc-framework/releases/download/v1.0.0/webrtc-win.tar.gz `
  -OutFile build/downloads/webrtc-win.tar.gz
tar -xzf build/downloads/webrtc-win.tar.gz -C vendor/windows
```

解压后应包含 `vendor/windows/webrtc-win/include/api/peer_connection_interface.h` 和
`vendor/windows/webrtc-win/lib/webrtc.lib`。若开发包已有独立安装目录，直接通过
`-DDOZER_WEBRTC_ROOT=D:/deps/webrtc-win` 使用，无需重复下载。

不要用 Jetson 归档、其它架构或任意新版本 WebRTC 替换。Windows SDK 固定使用 Release WebRTC 的
`/MD`、`NDEBUG` 和 `_ITERATOR_DEBUG_LEVEL=0`；Debug 操作台通过 C ABI 与该 SDK 交互。
本次验证使用的 `lib/webrtc.lib` SHA-256 为 `50c3afc213538b0fa0d486598a384129c0fb3a9e495ab4e79e87e78c2ad358ed`。
第三方软件的再分发须保留相应许可证。

NVDEC 使用与 rtc-framework 相同的 [NVIDIA Video Codec SDK](https://developer.nvidia.com/video-codec-sdk-archive) 11.0.10 及其 `NvDecoder` 示例，另需安装 CUDA Toolkit。将完整开发包解压至 `vendor/windows/Video_Codec_SDK_11.0.10/`，或传 `-DDOZER_NVCODEC_ROOT=D:/deps/Video_Codec_SDK_11.0.10`。目录中需有 `Interface/nvcuvid.h`、`Lib/x64/nvcuvid.lib` 和 `Samples/NvCodec/NvDecoder/`。工作站若已配置 rtc-framework，可直接指定它的 `third_party` 下对应两个开发包的目录；安装后的程序不依赖这些目录。
