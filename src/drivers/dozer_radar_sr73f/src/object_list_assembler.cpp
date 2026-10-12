#include "dozer_radar_sr73f/object_list_assembler.hpp"

#include <utility>

namespace dozer_radar_sr73f
{

ObjectListAssembler::ObjectListAssembler(
  uint8_t sensor_id, int64_t cycle_timeout_ns, Callback callback)
: sensor_id_(sensor_id), cycle_timeout_ns_(cycle_timeout_ns), callback_(std::move(callback))
{
}

void ObjectListAssembler::OnStatus(const ObjectListStatus & status, int64_t stamp_ns)
{
  if (collecting_) {
    // 上一周期的目标没有收齐就来了新的列表头
    Emit(false);
  }
  if (has_last_meas_count_ && status.meas_count != static_cast<uint16_t>(last_meas_count_ + 1)) {
    ++stats_.meas_count_gaps;
  }
  has_last_meas_count_ = true;
  last_meas_count_ = status.meas_count;

  current_ = ObjectCycle();
  current_.sensor_id = sensor_id_;
  current_.status = status;
  current_.stamp_ns = stamp_ns;
  current_.objects.reserve(status.nof_objects);
  collecting_ = true;
  if (status.nof_objects == 0) {
    Emit(true);
  }
}

void ObjectListAssembler::OnObject(const Object & object)
{
  if (!collecting_) {
    ++stats_.orphan_objects;
    return;
  }
  current_.objects.push_back(object);
  if (current_.objects.size() >= current_.status.nof_objects) {
    Emit(true);
  }
}

void ObjectListAssembler::Poll(int64_t now_ns)
{
  if (collecting_ && now_ns - current_.stamp_ns > cycle_timeout_ns_) {
    Emit(false);
  }
}

void ObjectListAssembler::Emit(bool complete)
{
  collecting_ = false;
  current_.complete = complete;
  if (complete) {
    ++stats_.complete_cycles;
  } else {
    ++stats_.incomplete_cycles;
  }
  if (callback_) {
    callback_(current_);
  }
}

}  // namespace dozer_radar_sr73f
