# Jetson 第三方开发包

本目录面向 Jetson Orin、Linux aarch64 和 JetPack 6。两个 zstd 归档是普通 Git 文件。colcon 在各自的构建目录解包，编译项目自己的 C API、RTC 会话、Jetson 编码器、相机、信令服务和应用。

| 归档 | 内容 | SHA-256 |
| --- | --- | --- |
| `webrtc-jetson.tar.zst` | 原 RTC 框架使用的 Jetson WebRTC Release 开发包，包含头文件、`lib/libwebrtc.a`、`LICENSE.md` | `84b9270199a3d18f4fd5dd27faae7e8db60153309c41511770bf0f7634e0cfd0` |
| `media-deps-aarch64.tar.zst` | 本机原发行配置的头文件、静态库及 CMake 包 | `61fbb6f8475d89dde90ff92c59f32dde4b91543877fcf0c2340d06adc21c1fe5` |

原 RTC 提交 `673cdc0` 记录的 WebRTC 开发包引用为 `295170cf1fd7b0e7f09f7e541ed9f824a662fa75`，本工程将实际使用的 Release 头文件和静态库纳入 Git。

媒体包包含 SDL 3.4.12、Dear ImGui v1.92.6（SDL3 和 OpenGL3 后端）、libyuv 1913（`6f729fbe658a40dfd993fa8b22bd612bb17cde5c`）、nanopb 0.4.9、Asio 1.32.0、nlohmann/json 3.12.0 和 spdlog 1.14.1。对应的 xmake-repo 配方版本为 `c90eaa1757e5aff2aa38fd773a32650d94e4349d`。许可证位于 `licenses/`。WebRTC 发行包自带的许可证随 SDK 安装。

NVIDIA 驱动、CUDA、Jetson Multimedia API 和 TensorRT 由 JetPack 提供。ROS、OpenCV、OpenSSL、OpenGL、V4L2、GStreamer 和 protobuf 使用系统开发包。
