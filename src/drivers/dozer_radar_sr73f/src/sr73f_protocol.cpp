#include "dozer_radar_sr73f/sr73f_protocol.hpp"

#include <cmath>
#include <cstdio>

namespace dozer_radar_sr73f
{

namespace
{

constexpr MessageType kAllTypes[] = {
  MessageType::kRadarCfg, MessageType::kRadarState, MessageType::kRegionCfg,
  MessageType::kRegionState, MessageType::kObjectStatus, MessageType::kObjectGeneral,
  MessageType::kVersion,
};

// 物理量 = 原始值 * res + offset
float Scale(uint32_t raw, float res, float offset)
{
  return static_cast<float>(raw) * res + offset;
}

// 原始值 = round((物理量 - offset) / res)，超出 [0, 2^length) 时返回 false。
bool Unscale(float value, float res, float offset, int length, uint32_t * raw)
{
  if (!std::isfinite(value)) {
    return false;
  }
  const double r = std::round((static_cast<double>(value) - offset) / res);
  if (r < 0.0 || r >= static_cast<double>(1u << length)) {
    return false;
  }
  *raw = static_cast<uint32_t>(r);
  return true;
}

void SetError(std::string * error, const std::string & message)
{
  if (error) {
    *error = message;
  }
}

// 距离类信号的分辨率与偏移（0x60B、0x401、0x402 共用）
constexpr float kDistRes = 0.2f;
constexpr float kLongOffset = -500.0f;
constexpr float kLatOffset = -204.6f;

}  // namespace

uint32_t MessageId(MessageType type, uint8_t sensor_id)
{
  return static_cast<uint32_t>(type) + static_cast<uint32_t>(sensor_id) * kSensorIdStride;
}

bool ParseMessageId(uint32_t can_id, MessageType * type, uint8_t * sensor_id)
{
  for (MessageType t : kAllTypes) {
    const uint32_t base = static_cast<uint32_t>(t);
    if (can_id < base) {
      continue;
    }
    const uint32_t diff = can_id - base;
    if (diff % kSensorIdStride == 0 && diff / kSensorIdStride <= kMaxSensorId) {
      *type = t;
      *sensor_id = static_cast<uint8_t>(diff / kSensorIdStride);
      return true;
    }
  }
  return false;
}

const char * MessageTypeName(MessageType type)
{
  switch (type) {
    case MessageType::kRadarCfg: return "RadarCfg";
    case MessageType::kRadarState: return "RadarState";
    case MessageType::kRegionCfg: return "RegionCfg";
    case MessageType::kRegionState: return "RegionState";
    case MessageType::kObjectStatus: return "ObjectStatus";
    case MessageType::kObjectGeneral: return "ObjectGeneral";
    case MessageType::kVersion: return "Version";
  }
  return "Unknown";
}

uint32_t GetMotorola(const uint8_t * data, int start_bit, int length)
{
  uint32_t value = 0;
  int pos = start_bit;
  for (int i = 0; i < length; ++i) {
    const int byte = pos / 8;
    const int bit = pos % 8;
    value = (value << 1) | ((data[byte] >> bit) & 0x1u);
    // Motorola 信号在字节内由高位向低位走，跨字节时进入下一字节的最高位
    pos = (bit == 0) ? (byte + 1) * 8 + 7 : pos - 1;
  }
  return value;
}

void SetMotorola(uint8_t * data, int start_bit, int length, uint32_t value)
{
  int pos = start_bit;
  for (int i = length - 1; i >= 0; --i) {
    const int byte = pos / 8;
    const int bit = pos % 8;
    const uint8_t mask = static_cast<uint8_t>(1u << bit);
    if ((value >> i) & 0x1u) {
      data[byte] |= mask;
    } else {
      data[byte] &= static_cast<uint8_t>(~mask);
    }
    pos = (bit == 0) ? (byte + 1) * 8 + 7 : pos - 1;
  }
}

bool DecodeObjectListStatus(const CanFrame & frame, ObjectListStatus * out)
{
  if (frame.dlc < 4) {
    return false;
  }
  const uint8_t * d = frame.data.data();
  out->nof_objects = static_cast<uint8_t>(GetMotorola(d, 7, 8));
  out->meas_count = static_cast<uint16_t>(GetMotorola(d, 15, 16));
  out->interface_version = static_cast<uint8_t>(GetMotorola(d, 31, 4));
  return true;
}

bool DecodeObject(const CanFrame & frame, Object * out)
{
  if (frame.dlc < 8) {
    return false;
  }
  const uint8_t * d = frame.data.data();
  out->id = static_cast<uint8_t>(GetMotorola(d, 7, 8));
  out->dist_long = Scale(GetMotorola(d, 15, 13), kDistRes, kLongOffset);
  out->dist_lat = Scale(GetMotorola(d, 18, 11), kDistRes, kLatOffset);
  out->vrel_long = Scale(GetMotorola(d, 39, 10), 0.25f, -128.0f);
  out->vrel_lat = Scale(GetMotorola(d, 45, 9), 0.25f, -64.0f);
  out->dyn_prop = static_cast<uint8_t>(GetMotorola(d, 50, 3));
  out->rcs = Scale(GetMotorola(d, 63, 8), 0.5f, -64.0f);
  return true;
}

bool DecodeRadarState(const CanFrame & frame, RadarState * out)
{
  if (frame.dlc < 8) {
    return false;
  }
  const uint8_t * d = frame.data.data();
  out->nvm_read_ok = GetMotorola(d, 6, 1) != 0;
  out->nvm_write_ok = GetMotorola(d, 7, 1) != 0;
  out->max_distance_m = static_cast<uint16_t>(GetMotorola(d, 15, 10) * 2);
  out->radar_power = static_cast<uint8_t>(GetMotorola(d, 25, 3));
  out->sensor_id = static_cast<uint8_t>(GetMotorola(d, 34, 3));
  out->sort_index = static_cast<uint8_t>(GetMotorola(d, 38, 3));
  out->output_type = static_cast<uint8_t>(GetMotorola(d, 43, 2));
  out->send_quality = GetMotorola(d, 44, 1) != 0;
  out->send_ext_info = GetMotorola(d, 45, 1) != 0;
  out->motion_rx_state = static_cast<uint8_t>(GetMotorola(d, 47, 2));
  out->can_baudrate = static_cast<uint8_t>(GetMotorola(d, 55, 3));
  out->rcs_threshold = static_cast<uint8_t>(GetMotorola(d, 60, 3));
  return true;
}

bool DecodeRegionState(const CanFrame & frame, RegionState * out)
{
  if (frame.dlc < 7) {
    return false;
  }
  const uint8_t * d = frame.data.data();
  out->max_output_number = static_cast<uint8_t>(GetMotorola(d, 5, 6));
  out->region_id = static_cast<uint8_t>(GetMotorola(d, 7, 2));
  out->region.point1_long = Scale(GetMotorola(d, 15, 13), kDistRes, kLongOffset);
  out->region.point1_lat = Scale(GetMotorola(d, 18, 11), kDistRes, kLatOffset);
  out->region.point2_long = Scale(GetMotorola(d, 39, 13), kDistRes, kLongOffset);
  out->region.point2_lat = Scale(GetMotorola(d, 42, 11), kDistRes, kLatOffset);
  return true;
}

bool DecodeSoftwareVersion(const CanFrame & frame, SoftwareVersion * out)
{
  if (frame.dlc < 3) {
    return false;
  }
  out->major = frame.data[0];
  out->minor = frame.data[1];
  out->patch = frame.data[2];
  return true;
}

const char * BaudRateName(uint8_t code)
{
  switch (code) {
    case 0: return "500K";
    case 1: return "250K";
    case 2: return "1M";
  }
  return "unknown";
}

bool EncodeRadarConfig(uint8_t target_sensor_id, const RadarConfig & cfg, CanFrame * out,
  std::string * error)
{
  if (target_sensor_id > kMaxSensorId || cfg.sensor_id > kMaxSensorId) {
    SetError(error, "SensorID 必须在 0-7 之间");
    return false;
  }
  if (cfg.rcs_threshold > 7 || cfg.sort_index > 7) {
    SetError(error, "RCS 门限与排序方式必须在 0-7 之间");
    return false;
  }
  CanFrame f;
  f.id = MessageId(MessageType::kRadarCfg, target_sensor_id);
  f.dlc = 8;
  uint8_t * d = f.data.data();
  // 未置 valid 的字段保持 0，与协议文档示例报文一致
  if (cfg.sensor_id_valid) {
    SetMotorola(d, 1, 1, 1);
    SetMotorola(d, 34, 3, cfg.sensor_id);
  }
  if (cfg.sort_index_valid) {
    SetMotorola(d, 6, 1, 1);
    SetMotorola(d, 46, 3, cfg.sort_index);
  }
  if (cfg.store_in_nvm) {
    SetMotorola(d, 7, 1, 1);
    SetMotorola(d, 47, 1, 1);
  }
  if (cfg.rcs_threshold_valid) {
    SetMotorola(d, 48, 1, 1);
    SetMotorola(d, 51, 3, cfg.rcs_threshold);
  }
  if (cfg.baudrate_valid) {
    SetMotorola(d, 60, 1, 1);
    SetMotorola(d, 63, 3, static_cast<uint8_t>(cfg.baudrate));
  }
  *out = f;
  return true;
}

bool IsRegionValid(const Region & r)
{
  return r.point1_long < r.point2_long && r.point1_lat > r.point2_lat;
}

bool EncodeRegionConfig(uint8_t target_sensor_id, const RegionConfig & cfg, CanFrame * out,
  std::string * error)
{
  if (target_sensor_id > kMaxSensorId) {
    SetError(error, "SensorID 必须在 0-7 之间");
    return false;
  }
  if (cfg.max_output_number > 63 || cfg.region_id > 7) {
    SetError(error, "最大目标数必须在 0-63 之间，区域 ID 必须在 0-7 之间");
    return false;
  }
  if (!IsRegionValid(cfg.region)) {
    SetError(error, "区域无效：要求 point1_long < point2_long 且 point1_lat > point2_lat"
      "（点 1 为右下角，点 2 为左上角，横向右正）");
    return false;
  }
  uint32_t p1_long, p1_lat, p2_long, p2_lat;
  if (!Unscale(cfg.region.point1_long, kDistRes, kLongOffset, 13, &p1_long) ||
    !Unscale(cfg.region.point1_lat, kDistRes, kLatOffset, 11, &p1_lat) ||
    !Unscale(cfg.region.point2_long, kDistRes, kLongOffset, 13, &p2_long) ||
    !Unscale(cfg.region.point2_lat, kDistRes, kLatOffset, 11, &p2_lat))
  {
    SetError(error, "区域坐标超出范围：纵向 [-500, 1138.2] m，横向 [-204.6, 204.8] m");
    return false;
  }
  CanFrame f;
  f.id = MessageId(MessageType::kRegionCfg, target_sensor_id);
  f.dlc = 8;
  uint8_t * d = f.data.data();
  SetMotorola(d, 5, 6, cfg.max_output_number);
  SetMotorola(d, 6, 1, cfg.activation ? 1 : 0);
  SetMotorola(d, 7, 1, cfg.coordinates_valid ? 1 : 0);
  SetMotorola(d, 10, 3, cfg.region_id);
  SetMotorola(d, 23, 13, p1_long);
  SetMotorola(d, 26, 11, p1_lat);
  SetMotorola(d, 47, 13, p2_long);
  SetMotorola(d, 50, 11, p2_lat);
  *out = f;
  return true;
}

std::string FormatFrame(const CanFrame & frame)
{
  char buf[64];
  int n = std::snprintf(buf, sizeof(buf), "%03X [%u]", frame.id, frame.dlc);
  for (int i = 0; i < frame.dlc && i < 8; ++i) {
    n += std::snprintf(buf + n, sizeof(buf) - n, " %02X", frame.data[i]);
  }
  return std::string(buf);
}

}  // namespace dozer_radar_sr73f
