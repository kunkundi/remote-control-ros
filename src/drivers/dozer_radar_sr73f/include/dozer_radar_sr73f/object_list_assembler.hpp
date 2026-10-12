#pragma once

#include <cstdint>
#include <functional>
#include <vector>

#include "dozer_radar_sr73f/sr73f_protocol.hpp"

namespace dozer_radar_sr73f
{

// 一台雷达一个测量周期的目标列表。
struct ObjectCycle
{
  uint8_t sensor_id = 0;
  ObjectListStatus status;
  std::vector<Object> objects;
  int64_t stamp_ns = 0;     // 收到 0x60A 列表头的时间
  bool complete = false;    // objects 数量等于列表头声明的数量
};

struct AssemblerStats
{
  uint64_t complete_cycles = 0;
  uint64_t incomplete_cycles = 0;   // 收到的目标少于列表头声明（丢帧或超时）
  uint64_t orphan_objects = 0;      // 没有对应列表头，或超出声明数量的 0x60B
  uint64_t meas_count_gaps = 0;     // 测量周期计数不连续的次数
};

// 把单台雷达的 0x60A + 若干 0x60B 拼成完整周期。
// 周期在以下任一情况结束并回调：收齐声明数量的目标；收到下一个 0x60A；超时。
// 非线程安全，由同一线程调用。
class ObjectListAssembler
{
public:
  using Callback = std::function<void (const ObjectCycle &)>;

  ObjectListAssembler(uint8_t sensor_id, int64_t cycle_timeout_ns, Callback callback);

  void OnStatus(const ObjectListStatus & status, int64_t stamp_ns);
  void OnObject(const Object & object);
  // 检查当前周期是否超时，now_ns 与 stamp_ns 使用同一时钟。
  void Poll(int64_t now_ns);

  const AssemblerStats & stats() const {return stats_;}

private:
  void Emit(bool complete);

  uint8_t sensor_id_;
  int64_t cycle_timeout_ns_;
  Callback callback_;
  bool collecting_ = false;
  bool has_last_meas_count_ = false;
  uint16_t last_meas_count_ = 0;
  ObjectCycle current_;
  AssemblerStats stats_;
};

}  // namespace dozer_radar_sr73f
