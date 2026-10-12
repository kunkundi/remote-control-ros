#include <gtest/gtest.h>

#include <random>

#include "dozer_radar_sr73f/sr73f_protocol.hpp"

using namespace dozer_radar_sr73f;

namespace
{

CanFrame MakeFrame(uint32_t id, std::initializer_list<uint8_t> bytes)
{
  CanFrame f;
  f.id = id;
  f.dlc = static_cast<uint8_t>(bytes.size());
  size_t i = 0;
  for (uint8_t b : bytes) {
    f.data[i++] = b;
  }
  return f;
}

void ExpectBytes(const CanFrame & f, std::initializer_list<uint8_t> bytes)
{
  ASSERT_EQ(f.dlc, bytes.size());
  size_t i = 0;
  for (uint8_t b : bytes) {
    EXPECT_EQ(f.data[i], b) << "byte " << i;
    ++i;
  }
}

}  // namespace

TEST(Sr73fProtocol, ParseMessageId)
{
  MessageType t;
  uint8_t sid;
  ASSERT_TRUE(ParseMessageId(0x60B, &t, &sid));
  EXPECT_EQ(t, MessageType::kObjectGeneral);
  EXPECT_EQ(sid, 0);
  ASSERT_TRUE(ParseMessageId(0x63A, &t, &sid));
  EXPECT_EQ(t, MessageType::kObjectStatus);
  EXPECT_EQ(sid, 3);
  ASSERT_TRUE(ParseMessageId(0x271, &t, &sid));
  EXPECT_EQ(t, MessageType::kRadarState);
  EXPECT_EQ(sid, 7);
  ASSERT_TRUE(ParseMessageId(0x710, &t, &sid));
  EXPECT_EQ(t, MessageType::kVersion);
  EXPECT_EQ(sid, 1);
  ASSERT_TRUE(ParseMessageId(0x200, &t, &sid));
  EXPECT_EQ(t, MessageType::kRadarCfg);
  EXPECT_FALSE(ParseMessageId(0x281, &t, &sid));   // SensorID 8 超出范围
  EXPECT_FALSE(ParseMessageId(0x123, &t, &sid));
  EXPECT_FALSE(ParseMessageId(0x60C, &t, &sid));
  EXPECT_EQ(MessageId(MessageType::kObjectGeneral, 2), 0x62Bu);
}

TEST(Sr73fProtocol, MotorolaRoundTrip)
{
  std::mt19937 rng(42);
  for (int start = 0; start < 64; ++start) {
    for (int len = 1; len <= 16; ++len) {
      // 信号不能越过第 8 字节末尾
      int pos = start;
      bool fits = true;
      for (int i = 1; i < len; ++i) {
        pos = (pos % 8 == 0) ? (pos / 8 + 1) * 8 + 7 : pos - 1;
        if (pos >= 64) {
          fits = false;
          break;
        }
      }
      if (!fits) {
        continue;
      }
      uint8_t d[8];
      for (auto & b : d) {
        b = static_cast<uint8_t>(rng());
      }
      const uint32_t v = rng() & ((1u << len) - 1);
      SetMotorola(d, start, len, v);
      EXPECT_EQ(GetMotorola(d, start, len), v) << "start=" << start << " len=" << len;
    }
  }
}

// 协议文档 7.4 节示例
TEST(Sr73fProtocol, DecodeObjectDocExample)
{
  const CanFrame f = MakeFrame(0x60B, {0x57, 0x4E, 0xC4, 0x0C, 0x7F, 0x60, 0x00, 0x80});
  Object o;
  ASSERT_TRUE(DecodeObject(f, &o));
  EXPECT_EQ(o.id, 87);
  EXPECT_NEAR(o.dist_long, 4.0f, 1e-3);
  EXPECT_NEAR(o.dist_lat, 2.6f, 1e-3);
  EXPECT_NEAR(o.vrel_long, -0.75f, 1e-4);
  EXPECT_NEAR(o.vrel_lat, 0.0f, 1e-4);
  EXPECT_EQ(o.dyn_prop, 0);
  EXPECT_NEAR(o.rcs, 0.0f, 1e-4);
}

// 与厂商 demo（get_target_data_sr73f_can.cpp 及协议 7.4 节公式）逐位对照
TEST(Sr73fProtocol, DecodeObjectMatchesVendorFormula)
{
  std::mt19937 rng(7);
  for (int n = 0; n < 2000; ++n) {
    CanFrame f;
    f.id = 0x60B;
    f.dlc = 8;
    for (auto & b : f.data) {
      b = static_cast<uint8_t>(rng());
    }
    const uint8_t * d = f.data.data();
    const float dist_long = (d[1] * 32 + (d[2] >> 3)) * 0.2f - 500.0f;
    const float dist_lat = ((d[2] & 0x07) * 256 + d[3]) * 0.2f - 204.6f;
    const float vrel_long = (d[4] * 4 + (d[5] >> 6)) * 0.25f - 128.0f;
    const float vrel_lat = ((d[5] & 0x3F) * 8 + (d[6] >> 5)) * 0.25f - 64.0f;
    const int dyn = d[6] & 0x07;
    const float rcs = d[7] * 0.5f - 64.0f;
    Object o;
    ASSERT_TRUE(DecodeObject(f, &o));
    EXPECT_EQ(o.id, d[0]);
    EXPECT_NEAR(o.dist_long, dist_long, 1e-3);
    EXPECT_NEAR(o.dist_lat, dist_lat, 1e-3);
    EXPECT_NEAR(o.vrel_long, vrel_long, 1e-4);
    EXPECT_NEAR(o.vrel_lat, vrel_lat, 1e-4);
    EXPECT_EQ(o.dyn_prop, dyn);
    EXPECT_NEAR(o.rcs, rcs, 1e-4);
  }
}

TEST(Sr73fProtocol, DecodeObjectRejectsShortFrame)
{
  CanFrame f = MakeFrame(0x60B, {0x57, 0x4E, 0xC4, 0x0C, 0x7F, 0x60, 0x00});
  Object o;
  EXPECT_FALSE(DecodeObject(f, &o));
}

TEST(Sr73fProtocol, DecodeObjectListStatus)
{
  const CanFrame f = MakeFrame(0x60A, {0x0A, 0x01, 0x02, 0x30});
  ObjectListStatus s;
  ASSERT_TRUE(DecodeObjectListStatus(f, &s));
  EXPECT_EQ(s.nof_objects, 10);
  EXPECT_EQ(s.meas_count, 0x0102);
  EXPECT_EQ(s.interface_version, 3);
}

// 按协议 5.1 节表格（最低位起始）手工排布字节，验证与 DBC 位置一致
TEST(Sr73fProtocol, DecodeRadarState)
{
  // nvm_read=1；max_distance=160m(原始 80)；sensor_id=3；sort=1；output=1；
  // motion_rx=3；baud=2(1M)；rcs_threshold=1
  const CanFrame f = MakeFrame(0x231, {0x40, 0x14, 0x00, 0x00, 0x13, 0xC4, 0x40, 0x04});
  RadarState s;
  ASSERT_TRUE(DecodeRadarState(f, &s));
  EXPECT_TRUE(s.nvm_read_ok);
  EXPECT_FALSE(s.nvm_write_ok);
  EXPECT_EQ(s.max_distance_m, 160);
  EXPECT_EQ(s.sensor_id, 3);
  EXPECT_EQ(s.sort_index, 1);
  EXPECT_EQ(s.radar_power, 0);
  EXPECT_EQ(s.output_type, 1);
  EXPECT_FALSE(s.send_quality);
  EXPECT_FALSE(s.send_ext_info);
  EXPECT_EQ(s.motion_rx_state, 3);
  EXPECT_EQ(s.can_baudrate, 2);
  EXPECT_EQ(s.rcs_threshold, 1);
}

// 按厂商“区域设置 402 C 语言解析示例”的字节布局构造
TEST(Sr73fProtocol, DecodeRegionState)
{
  // max=63，region_id=1，P1=(0,3)，P2=(20,-3)
  const CanFrame f = MakeFrame(0x402, {0x7F, 0x4E, 0x24, 0x0E, 0x51, 0x43, 0xF0, 0x00});
  RegionState s;
  ASSERT_TRUE(DecodeRegionState(f, &s));
  EXPECT_EQ(s.max_output_number, 63);
  EXPECT_EQ(s.region_id, 1);
  EXPECT_NEAR(s.region.point1_long, 0.0f, 1e-3);
  EXPECT_NEAR(s.region.point1_lat, 3.0f, 1e-3);
  EXPECT_NEAR(s.region.point2_long, 20.0f, 1e-3);
  EXPECT_NEAR(s.region.point2_lat, -3.0f, 1e-3);
}

TEST(Sr73fProtocol, DecodeSoftwareVersion)
{
  const CanFrame f = MakeFrame(0x700, {0x01, 0x00, 0x15, 0, 0, 0, 0, 0});
  SoftwareVersion v;
  ASSERT_TRUE(DecodeSoftwareVersion(f, &v));
  EXPECT_EQ(v.major, 1);
  EXPECT_EQ(v.minor, 0);
  EXPECT_EQ(v.patch, 21);
}

// 协议文档 7.1/7.3 节：修改雷达 ID 为 1 并保存
TEST(Sr73fProtocol, EncodeSetIdDocExample)
{
  RadarConfig cfg;
  cfg.sensor_id_valid = true;
  cfg.sensor_id = 1;
  cfg.store_in_nvm = true;
  CanFrame f;
  ASSERT_TRUE(EncodeRadarConfig(0, cfg, &f, nullptr));
  EXPECT_EQ(f.id, 0x200u);
  ExpectBytes(f, {0x82, 0x00, 0x00, 0x00, 0x01, 0x80, 0x00, 0x00});

  cfg.sensor_id = 3;
  ASSERT_TRUE(EncodeRadarConfig(0, cfg, &f, nullptr));
  ExpectBytes(f, {0x82, 0x00, 0x00, 0x00, 0x03, 0x80, 0x00, 0x00});
}

TEST(Sr73fProtocol, EncodeSetBaud)
{
  RadarConfig cfg;
  cfg.baudrate_valid = true;
  cfg.baudrate = BaudRate::k1M;
  cfg.store_in_nvm = true;
  CanFrame f;
  ASSERT_TRUE(EncodeRadarConfig(2, cfg, &f, nullptr));
  EXPECT_EQ(f.id, 0x220u);
  // BaudRate_Valid 为第 7 字节 bit4，BaudRate 为第 7 字节 bit7..5
  ExpectBytes(f, {0x80, 0x00, 0x00, 0x00, 0x00, 0x80, 0x00, 0x50});
}

// 协议文档 7.5 节两个示例
TEST(Sr73fProtocol, EncodeRegionDocExamples)
{
  RegionConfig cfg;
  cfg.max_output_number = 63;
  cfg.region = Region{0.0f, 3.0f, 20.0f, -3.0f};
  CanFrame f;
  std::string error;
  ASSERT_TRUE(EncodeRegionConfig(0, cfg, &f, &error)) << error;
  EXPECT_EQ(f.id, 0x401u);
  ExpectBytes(f, {0xFF, 0x01, 0x4E, 0x24, 0x0E, 0x51, 0x43, 0xF0});

  cfg.region = Region{0.0f, 5.0f, 50.0f, -5.0f};
  ASSERT_TRUE(EncodeRegionConfig(0, cfg, &f, &error)) << error;
  ExpectBytes(f, {0xFF, 0x01, 0x4E, 0x24, 0x18, 0x55, 0xF3, 0xE6});

  ASSERT_TRUE(EncodeRegionConfig(1, cfg, &f, &error)) << error;
  EXPECT_EQ(f.id, 0x411u);
}

TEST(Sr73fProtocol, EncodeRegionRejectsInvalid)
{
  RegionConfig cfg;
  CanFrame f;
  cfg.region = Region{20.0f, 3.0f, 0.0f, -3.0f};   // 纵向顺序反了
  EXPECT_FALSE(EncodeRegionConfig(0, cfg, &f, nullptr));
  cfg.region = Region{0.0f, -3.0f, 20.0f, 3.0f};   // 横向顺序反了
  EXPECT_FALSE(EncodeRegionConfig(0, cfg, &f, nullptr));
  cfg.region = Region{0.0f, 3.0f, 2000.0f, -3.0f}; // 超出范围
  EXPECT_FALSE(EncodeRegionConfig(0, cfg, &f, nullptr));
  cfg.region = Region{0.0f, 3.0f, 20.0f, -3.0f};
  cfg.max_output_number = 64;
  EXPECT_FALSE(EncodeRegionConfig(0, cfg, &f, nullptr));
  cfg.max_output_number = 30;
  EXPECT_FALSE(EncodeRegionConfig(8, cfg, &f, nullptr));
}
