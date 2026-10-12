#pragma once

#include <array>
#include <cstdint>
#include <string>

// SR73F 毫米波雷达 CAN 协议（纳雷 SR73F 通信协议 V1.7 与 SR73F_CAN_database_ID0.dbc）。
// 本文件与 ROS、硬件均无关，可直接用于单元测试和离线工具。
//
// 信号位置统一采用 DBC 约定：Motorola（大端）字节序，start_bit 为信号最高位所在位置
// （字节号 * 8 + 字节内位号）。协议 PDF 表格中的 Start 列是最低位位置，二者已逐一核对一致。

namespace dozer_radar_sr73f
{

struct CanFrame
{
  uint32_t id = 0;              // 11 位标准帧 ID
  uint8_t dlc = 0;
  std::array<uint8_t, 8> data{};
  int64_t stamp_ns = 0;         // 接收时间（CLOCK_REALTIME 纳秒），0 表示未知
};

constexpr uint8_t kMaxSensorId = 7;
constexpr uint32_t kSensorIdStride = 0x10;

// 各消息在 SensorID=0 时的 ID；实际 ID = 基础 ID + SensorID * 0x10。
enum class MessageType : uint32_t
{
  kRadarCfg = 0x200,        // 输入：雷达配置
  kRadarState = 0x201,      // 输出：雷达状态，约 1 Hz
  kRegionCfg = 0x401,       // 输入：区域（矩形框）与最大目标数配置
  kRegionState = 0x402,     // 输出：区域状态，区域启用时约 1 Hz
  kObjectStatus = 0x60A,    // 输出：目标列表头，每个测量周期（约 33 ms）先发
  kObjectGeneral = 0x60B,   // 输出：单个目标
  kVersion = 0x700,         // 输出：软件版本（心跳）
};

uint32_t MessageId(MessageType type, uint8_t sensor_id);

// 识别 CAN ID 属于哪类 SR73F 消息及其 SensorID；不是 SR73F 消息时返回 false。
bool ParseMessageId(uint32_t can_id, MessageType * type, uint8_t * sensor_id);

const char * MessageTypeName(MessageType type);

// Motorola 信号读写，start_bit 为信号最高位位置（DBC 约定）。
uint32_t GetMotorola(const uint8_t * data, int start_bit, int length);
void SetMotorola(uint8_t * data, int start_bit, int length, uint32_t value);

// ---------- 输出消息 ----------

struct ObjectListStatus
{
  uint8_t nof_objects = 0;
  uint16_t meas_count = 0;
  uint8_t interface_version = 0;
};

// 单个目标，坐标为雷达原始坐标：纵向向前为正，横向向右为正（与 REP-103 的 y 方向相反）。
struct Object
{
  uint8_t id = 0;
  float dist_long = 0.0f;   // m
  float dist_lat = 0.0f;    // m，右正
  float vrel_long = 0.0f;   // m/s，远离为正
  float vrel_lat = 0.0f;    // m/s，从左往右为正
  uint8_t dyn_prop = 0;     // 当前固件未开放，恒为 0
  float rcs = 0.0f;         // dBsm
};

struct RadarState
{
  bool nvm_read_ok = false;
  bool nvm_write_ok = false;
  uint16_t max_distance_m = 0;
  uint8_t sensor_id = 0;
  uint8_t sort_index = 0;
  uint8_t radar_power = 0;
  uint8_t output_type = 0;
  bool send_quality = false;
  bool send_ext_info = false;
  uint8_t motion_rx_state = 0;
  uint8_t can_baudrate = 0;   // 0:500K 1:250K 2:1M
  uint8_t rcs_threshold = 0;
};

struct Region
{
  float point1_long = 0.0f;   // 点 1：矩形框右下角
  float point1_lat = 0.0f;
  float point2_long = 0.0f;   // 点 2：矩形框左上角
  float point2_lat = 0.0f;
};

struct RegionState
{
  uint8_t max_output_number = 0;
  uint8_t region_id = 0;
  Region region;
};

struct SoftwareVersion
{
  uint8_t major = 0;
  uint8_t minor = 0;
  uint8_t patch = 0;
};

// 解码函数：DLC 不足时返回 false，不修改输出。
bool DecodeObjectListStatus(const CanFrame & frame, ObjectListStatus * out);
bool DecodeObject(const CanFrame & frame, Object * out);
bool DecodeRadarState(const CanFrame & frame, RadarState * out);
bool DecodeRegionState(const CanFrame & frame, RegionState * out);
bool DecodeSoftwareVersion(const CanFrame & frame, SoftwareVersion * out);

// ---------- 输入消息（配置） ----------

enum class BaudRate : uint8_t { k500K = 0, k250K = 1, k1M = 2 };

const char * BaudRateName(uint8_t code);

// 0x200 配置：只有对应 valid 为 true 的字段会被雷达采纳。
struct RadarConfig
{
  bool sensor_id_valid = false;
  uint8_t sensor_id = 0;
  bool baudrate_valid = false;
  BaudRate baudrate = BaudRate::k500K;
  bool rcs_threshold_valid = false;
  uint8_t rcs_threshold = 0;
  bool sort_index_valid = false;
  uint8_t sort_index = 1;
  bool store_in_nvm = false;  // 写入非易失存储，掉电保持；会消耗 NVM 寿命，勿频繁使用
};

// 0x401 区域配置。雷达只在 point1_long < point2_long 且 point1_lat > point2_lat 时保存。
struct RegionConfig
{
  uint8_t max_output_number = 63;   // 0-63
  bool activation = true;
  bool coordinates_valid = true;
  uint8_t region_id = 1;
  Region region;
};

// 编码函数：参数越界时返回 false 并写入 error（可为 nullptr）。
bool EncodeRadarConfig(uint8_t target_sensor_id, const RadarConfig & cfg, CanFrame * out,
  std::string * error);
bool EncodeRegionConfig(uint8_t target_sensor_id, const RegionConfig & cfg, CanFrame * out,
  std::string * error);

// 校验区域坐标是否满足雷达的保存条件。
bool IsRegionValid(const Region & region);

std::string FormatFrame(const CanFrame & frame);

}  // namespace dozer_radar_sr73f
