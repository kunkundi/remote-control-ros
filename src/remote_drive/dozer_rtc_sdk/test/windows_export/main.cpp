#include <cstring>

namespace Audio {}
#define RTC_API 42
#include <VtsRtcApi.h>
static_assert(RTC_API == 42, "SDK header must preserve the consumer's module export macro");

int main() {
  if (std::strcmp(VtsRtc::RtcErrorMessage(VtsRtc::OK), "OK") != 0) return 1;
  VtsRtc::RtcVideoDevices devices = nullptr;
  size_t count = 0;
  return VtsRtc::RtcGetVideoDevices(&devices, &count) == VtsRtc::AgentNotInited ? 0 : 1;
}
