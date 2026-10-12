#pragma once

#include <cstdint>
#include <fstream>
#include <string>
#include <vector>

#include "dozer_radar_sr73f/sr73f_protocol.hpp"

namespace dozer_radar_sr73f
{

// CAN 总线错误帧统计（SocketCAN error frame）。
struct BusErrorStats
{
  uint64_t error_frames = 0;
  uint64_t bus_off = 0;
  uint64_t controller_problems = 0;   // 含 error passive / warning、RX/TX 溢出
  uint64_t no_ack = 0;                // 发送无应答：总线上没有其它节点或波特率不一致
  uint64_t protocol_violations = 0;   // 位错误、格式错误等，常见于波特率不匹配或终端电阻问题
};

class CanFrameSource
{
public:
  virtual ~CanFrameSource() = default;
  // 读取一帧。超时返回 false 且 error 为空；出错返回 false 且 error 非空。
  virtual bool Read(CanFrame * frame, int timeout_ms, std::string * error) = 0;
  virtual bool Write(const CanFrame & frame, std::string * error) = 0;
  virtual BusErrorStats bus_errors() const {return BusErrorStats();}
  virtual std::string description() const = 0;
};

// SocketCAN 原始套接字。接收时间戳取内核时间戳（SO_TIMESTAMPNS，CLOCK_REALTIME）。
class SocketCanSource : public CanFrameSource
{
public:
  SocketCanSource() = default;
  ~SocketCanSource() override;
  SocketCanSource(const SocketCanSource &) = delete;
  SocketCanSource & operator=(const SocketCanSource &) = delete;

  bool Open(const std::string & interface, std::string * error);
  bool Read(CanFrame * frame, int timeout_ms, std::string * error) override;
  bool Write(const CanFrame & frame, std::string * error) override;
  BusErrorStats bus_errors() const override {return bus_errors_;}
  std::string description() const override {return "socketcan:" + interface_;}

private:
  void HandleErrorFrame(uint32_t can_id, const uint8_t * data);

  int fd_ = -1;
  std::string interface_;
  BusErrorStats bus_errors_;
};

// 回放 candump -l 格式的日志，例如 "(1697123456.123456) can0 60A#0A00010000000000"。
// 按日志中的时间间隔回放，帧时间戳改写为当前时间，便于下游按实时数据处理。
class CandumpReplaySource : public CanFrameSource
{
public:
  bool Open(const std::string & path, bool loop, std::string * error);
  bool Read(CanFrame * frame, int timeout_ms, std::string * error) override;
  bool Write(const CanFrame & frame, std::string * error) override;
  std::string description() const override {return "replay:" + path_;}

private:
  struct Entry
  {
    int64_t log_stamp_ns;
    CanFrame frame;
  };

  std::string path_;
  bool loop_ = false;
  std::vector<Entry> entries_;
  size_t next_ = 0;
  int64_t base_log_ns_ = 0;
  int64_t base_wall_ns_ = 0;
};

// 解析一行 candump -l 日志；只接受 11 位标准数据帧，其余返回 false。
bool ParseCandumpLine(const std::string & line, int64_t * stamp_ns, CanFrame * frame);

int64_t RealtimeNowNs();

}  // namespace dozer_radar_sr73f
