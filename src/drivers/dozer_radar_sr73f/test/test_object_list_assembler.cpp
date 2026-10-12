#include <gtest/gtest.h>

#include <vector>

#include "dozer_radar_sr73f/object_list_assembler.hpp"

using namespace dozer_radar_sr73f;

namespace
{

constexpr int64_t kMs = 1000000LL;

ObjectListStatus Status(uint8_t nof, uint16_t meas)
{
  ObjectListStatus s;
  s.nof_objects = nof;
  s.meas_count = meas;
  return s;
}

Object Obj(uint8_t id)
{
  Object o;
  o.id = id;
  return o;
}

class AssemblerTest : public ::testing::Test
{
protected:
  AssemblerTest()
  : assembler_(2, 100 * kMs, [this](const ObjectCycle & c) {cycles_.push_back(c);})
  {
  }

  std::vector<ObjectCycle> cycles_;
  ObjectListAssembler assembler_;
};

}  // namespace

TEST_F(AssemblerTest, CompleteCycleEmittedWhenAllObjectsArrive)
{
  assembler_.OnStatus(Status(3, 10), 1000 * kMs);
  assembler_.OnObject(Obj(1));
  assembler_.OnObject(Obj(2));
  EXPECT_TRUE(cycles_.empty());
  assembler_.OnObject(Obj(3));
  ASSERT_EQ(cycles_.size(), 1u);
  EXPECT_TRUE(cycles_[0].complete);
  EXPECT_EQ(cycles_[0].sensor_id, 2);
  EXPECT_EQ(cycles_[0].stamp_ns, 1000 * kMs);
  EXPECT_EQ(cycles_[0].status.meas_count, 10);
  ASSERT_EQ(cycles_[0].objects.size(), 3u);
  EXPECT_EQ(cycles_[0].objects[2].id, 3);
  EXPECT_EQ(assembler_.stats().complete_cycles, 1u);
}

TEST_F(AssemblerTest, EmptyCycleEmittedImmediately)
{
  assembler_.OnStatus(Status(0, 1), 0);
  ASSERT_EQ(cycles_.size(), 1u);
  EXPECT_TRUE(cycles_[0].complete);
  EXPECT_TRUE(cycles_[0].objects.empty());
}

TEST_F(AssemblerTest, MissingObjectsFlushedByNextHeader)
{
  assembler_.OnStatus(Status(3, 1), 0);
  assembler_.OnObject(Obj(1));
  assembler_.OnStatus(Status(1, 2), 33 * kMs);
  ASSERT_EQ(cycles_.size(), 1u);
  EXPECT_FALSE(cycles_[0].complete);
  EXPECT_EQ(cycles_[0].objects.size(), 1u);
  assembler_.OnObject(Obj(9));
  ASSERT_EQ(cycles_.size(), 2u);
  EXPECT_TRUE(cycles_[1].complete);
  EXPECT_EQ(assembler_.stats().incomplete_cycles, 1u);
  EXPECT_EQ(assembler_.stats().complete_cycles, 1u);
}

TEST_F(AssemblerTest, TimeoutFlushesIncompleteCycle)
{
  assembler_.OnStatus(Status(2, 1), 0);
  assembler_.OnObject(Obj(1));
  assembler_.Poll(50 * kMs);
  EXPECT_TRUE(cycles_.empty());
  assembler_.Poll(150 * kMs);
  ASSERT_EQ(cycles_.size(), 1u);
  EXPECT_FALSE(cycles_[0].complete);
  // 超时后迟到的目标计为孤立目标
  assembler_.OnObject(Obj(2));
  EXPECT_EQ(assembler_.stats().orphan_objects, 1u);
}

TEST_F(AssemblerTest, ObjectsWithoutHeaderAreOrphans)
{
  assembler_.OnObject(Obj(1));
  assembler_.OnObject(Obj(2));
  EXPECT_TRUE(cycles_.empty());
  EXPECT_EQ(assembler_.stats().orphan_objects, 2u);
}

TEST_F(AssemblerTest, ExtraObjectsAfterCompletionAreOrphans)
{
  assembler_.OnStatus(Status(1, 1), 0);
  assembler_.OnObject(Obj(1));
  assembler_.OnObject(Obj(2));
  EXPECT_EQ(cycles_.size(), 1u);
  EXPECT_EQ(assembler_.stats().orphan_objects, 1u);
}

TEST_F(AssemblerTest, MeasCountGapsCounted)
{
  assembler_.OnStatus(Status(0, 65534), 0);
  assembler_.OnStatus(Status(0, 65535), 0);
  assembler_.OnStatus(Status(0, 0), 0);    // 回绕不算断档
  EXPECT_EQ(assembler_.stats().meas_count_gaps, 0u);
  assembler_.OnStatus(Status(0, 5), 0);
  EXPECT_EQ(assembler_.stats().meas_count_gaps, 1u);
}
