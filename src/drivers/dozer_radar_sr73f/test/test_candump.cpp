#include <gtest/gtest.h>

#include "dozer_radar_sr73f/can_frame_source.hpp"

using namespace dozer_radar_sr73f;

TEST(Candump, ParsesStandardFrame)
{
  int64_t ns = 0;
  CanFrame f;
  ASSERT_TRUE(ParseCandumpLine("(1697123456.123456) can0 60B#574EC40C7F600080", &ns, &f));
  EXPECT_EQ(ns, 1697123456123456000LL);
  EXPECT_EQ(f.id, 0x60Bu);
  EXPECT_EQ(f.dlc, 8);
  EXPECT_EQ(f.data[0], 0x57);
  EXPECT_EQ(f.data[7], 0x80);
}

TEST(Candump, ParsesShortAndEmptyPayload)
{
  int64_t ns = 0;
  CanFrame f;
  ASSERT_TRUE(ParseCandumpLine("(0.5) vcan0 60A#0A000102", &ns, &f));
  EXPECT_EQ(ns, 500000000LL);
  EXPECT_EQ(f.dlc, 4);
  ASSERT_TRUE(ParseCandumpLine("(1.000001) can0 700#", &ns, &f));
  EXPECT_EQ(f.dlc, 0);
}

TEST(Candump, RejectsUnsupported)
{
  int64_t ns = 0;
  CanFrame f;
  EXPECT_FALSE(ParseCandumpLine("", &ns, &f));
  EXPECT_FALSE(ParseCandumpLine("# comment", &ns, &f));
  EXPECT_FALSE(ParseCandumpLine("(1.0) can0 12345678#00", &ns, &f));   // 扩展帧
  EXPECT_FALSE(ParseCandumpLine("(1.0) can0 60B#R", &ns, &f));         // 远程帧
  EXPECT_FALSE(ParseCandumpLine("(1.0) can0 60B##0112233", &ns, &f));  // CAN FD
  EXPECT_FALSE(ParseCandumpLine("(1.0) can0 60B#001122334455667788", &ns, &f));  // 超过 8 字节
  EXPECT_FALSE(ParseCandumpLine("(1.0) can0 G0B#00", &ns, &f));
}
