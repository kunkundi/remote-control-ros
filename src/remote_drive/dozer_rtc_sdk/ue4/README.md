# 在 UE4.27 Win64 中使用 VtsRtc

此包提供 RTC 会话、音视频收发（含 NVIDIA NVDEC）和数据通道。原操作台的车辆指令、视图切换和融合检测消息格式由 `dozer_rtc_common` 定义，不属于 DLL 的导出接口。

将压缩包中的整个 `VtsRtc/` 解压到 **项目的 `Source/ThirdParty/`**。保留 `VtsRtc.Build.cs`、`include/`、`lib/Win64/` 和 `bin/Win64/` 的相对位置。游戏或插件模块的 `.Build.cs` 构造函数中加入：

```csharp
PrivateDependencyModuleNames.Add("VtsRtc");
```

重新生成 UE 工程文件并编译。外部模块会链接 `vtsrtc.lib`，把 `vtsrtc.dll` 复制到项目的 `Binaries/Win64/`，并将其作为 NonUFS 文件随游戏打包。开发人员使用导出的包即可，不需要 ROS、CUDA Toolkit、SDL 或 WebRTC 头文件。运行机需要 x64 Visual C++ 2015–2022 Redistributable；H.264 硬解需要支持 NVDEC 的 NVIDIA 显卡和驱动。

在调用方模块的 `.cpp` 中只包含 `VtsRtcApi.h`，用 `VtsRtc::` 访问接口。此包装头隔离 SDK 的全局枚举（例如 `Audio`），不要在同一 UE 模块中混用裸 `c_rtc.h`。不要定义 `RTC_DLL_EXPORTS`。下面的调用可以验证加载和链接，不会连接服务器：

```cpp
#include "CoreMinimal.h"
#include "VtsRtcApi.h"
#include "HAL/PlatformProcess.h"
#include "Misc/Paths.h"

// 在模块启动时保存句柄，确保 Editor 和打包程序都从项目目录加载 DLL。
const FString DllPath = FPaths::ConvertRelativePathToFull(
    FPaths::Combine(FPaths::ProjectDir(), TEXT("Binaries/Win64/vtsrtc.dll")));
void* RtcDll = FPlatformProcess::GetDllHandle(*DllPath);
if (RtcDll)
{
    UE_LOG(LogTemp, Log, TEXT("VtsRtc: %s"), UTF8_TO_TCHAR(VtsRtc::RtcErrorMessage(VtsRtc::OK)));
}
```

要接收真实视频，将 `config/rtc.example.cfg` 复制为项目的 `Config/rtc.cfg` 并填入实际服务地址。示例只含回环地址，不含认证信息。若由 UE 打包配置文件，请在游戏模块中添加 `RuntimeDependencies.Add("$(ProjectDir)/Config/rtc.cfg", StagedFileType.NonUFS);`：SDK 按文件路径读取 JSON，不能直接读取 `.pak` 内的资源。

把配置文件的绝对路径转成 UTF-8，填入零初始化的 `VtsRtc::RtcInitParams`，注册回调并调用 `VtsRtc::RtcInitAgentV2`。保留路径字符串至 Agent 销毁；音视频和消息回调来自 SDK 线程，返回之前复制需保留的数据，再投递到 UE 对应线程。SDK 在进程中只维护一个 Agent，PIE 多实例不能各自初始化一个。结束 PIE 或退出模块时停止业务、调用 `VtsRtc::RtcDestoryAgent()`（接口原名），清理已投递的任务，并在不再使用 SDK 时释放 DLL 句柄。
