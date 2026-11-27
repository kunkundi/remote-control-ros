#pragma once

#include <stddef.h>

#pragma push_macro("RTC_API")
#undef RTC_API
namespace VtsRtc {
#include "vtsrtc/c_rtc.h"
}
#pragma pop_macro("RTC_API")
