#pragma once

#include <cstddef>

namespace dozer::perception::v1 {

inline constexpr char kFusedDetectionChannel[] = "perception.fused.v1";
inline constexpr std::size_t kMaxFusedSnapshotBytes = 16 * 1024;
inline constexpr char kCameraUpdatePrefix[] = "camera:";

}