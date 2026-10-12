#include "dozer_radar_sr73f/can_frame_source.hpp"

#include <linux/can.h>
#include <linux/can/error.h>
#include <linux/can/raw.h>
#include <net/if.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <chrono>
#include <cstring>
#include <ctime>
#include <thread>

namespace dozer_radar_sr73f
{

namespace
{

void SetError(std::string * error, const std::string & message)
{
  if (error) {
    *error = message;
  }
}

std::string ErrnoText(const char * what)
{
  return std::string(what) + ": " + std::strerror(errno);
}

int HexValue(char c)
{
  if (c >= '0' && c <= '9') {
    return c - '0';
  }
  if (c >= 'a' && c <= 'f') {
    return c - 'a' + 10;
  }
  if (c >= 'A' && c <= 'F') {
    return c - 'A' + 10;
  }
  return -1;
}

}  // namespace

int64_t RealtimeNowNs()
{
  timespec ts{};
  clock_gettime(CLOCK_REALTIME, &ts);
  return static_cast<int64_t>(ts.tv_sec) * 1000000000LL + ts.tv_nsec;
}

// ---------------- SocketCanSource ----------------

SocketCanSource::~SocketCanSource()
{
  if (fd_ >= 0) {
    close(fd_);
  }
}

bool SocketCanSource::Open(const std::string & interface, std::string * error)
{
  interface_ = interface;
  fd_ = socket(PF_CAN, SOCK_RAW, CAN_RAW);
  if (fd_ < 0) {
    if (errno == EAFNOSUPPORT || errno == EPROTONOSUPPORT) {
      SetError(error, ErrnoText("创建 CAN 套接字失败") +
        "（内核未加载 SocketCAN 模块，执行 sudo modprobe can_raw）");
    } else {
      SetError(error, ErrnoText("创建 CAN 套接字失败"));
    }
    return false;
  }
  if (interface.size() >= IFNAMSIZ) {
    SetError(error, "CAN 接口名过长: " + interface);
    return false;
  }
  ifreq ifr{};
  std::strncpy(ifr.ifr_name, interface.c_str(), IFNAMSIZ - 1);
  if (ioctl(fd_, SIOCGIFINDEX, &ifr) < 0) {
    SetError(error, ErrnoText(("找不到 CAN 接口 " + interface).c_str()));
    return false;
  }
  sockaddr_can addr{};
  addr.can_family = AF_CAN;
  addr.can_ifindex = ifr.ifr_ifindex;
  if (bind(fd_, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) < 0) {
    SetError(error, ErrnoText("绑定 CAN 接口失败"));
    return false;
  }
  // 接收全部错误帧，用于诊断总线状态（bus-off、无应答、波特率不匹配等）
  can_err_mask_t err_mask = CAN_ERR_MASK;
  setsockopt(fd_, SOL_CAN_RAW, CAN_RAW_ERR_FILTER, &err_mask, sizeof(err_mask));
  int on = 1;
  setsockopt(fd_, SOL_SOCKET, SO_TIMESTAMPNS, &on, sizeof(on));
  // 4 台雷达满负载约 8000 帧/s，放大接收缓冲区避免处理抖动时丢帧
  int rcvbuf = 4 * 1024 * 1024;
  setsockopt(fd_, SOL_SOCKET, SO_RCVBUF, &rcvbuf, sizeof(rcvbuf));
  return true;
}

bool SocketCanSource::Read(CanFrame * frame, int timeout_ms, std::string * error)
{
  pollfd pfd{fd_, POLLIN, 0};
  const int ret = poll(&pfd, 1, timeout_ms);
  if (ret < 0) {
    if (errno == EINTR) {
      return false;
    }
    SetError(error, ErrnoText("poll CAN 套接字失败"));
    return false;
  }
  if (ret == 0) {
    return false;
  }

  can_frame cf{};
  iovec iov{&cf, sizeof(cf)};
  alignas(cmsghdr) char control[CMSG_SPACE(sizeof(timespec))];
  msghdr msg{};
  msg.msg_iov = &iov;
  msg.msg_iovlen = 1;
  msg.msg_control = control;
  msg.msg_controllen = sizeof(control);
  const ssize_t n = recvmsg(fd_, &msg, 0);
  if (n < 0) {
    if (errno == EINTR || errno == EAGAIN) {
      return false;
    }
    SetError(error, ErrnoText("读取 CAN 帧失败"));
    return false;
  }
  if (static_cast<size_t>(n) < sizeof(cf)) {
    return false;
  }
  if (cf.can_id & CAN_ERR_FLAG) {
    HandleErrorFrame(cf.can_id, cf.data);
    return false;
  }
  if ((cf.can_id & CAN_EFF_FLAG) || (cf.can_id & CAN_RTR_FLAG)) {
    return false;
  }

  frame->id = cf.can_id & CAN_SFF_MASK;
  frame->dlc = cf.can_dlc > 8 ? 8 : cf.can_dlc;
  frame->data.fill(0);
  std::memcpy(frame->data.data(), cf.data, frame->dlc);
  frame->stamp_ns = 0;
  for (cmsghdr * c = CMSG_FIRSTHDR(&msg); c != nullptr; c = CMSG_NXTHDR(&msg, c)) {
    if (c->cmsg_level == SOL_SOCKET && c->cmsg_type == SO_TIMESTAMPNS) {
      timespec ts{};
      std::memcpy(&ts, CMSG_DATA(c), sizeof(ts));
      frame->stamp_ns = static_cast<int64_t>(ts.tv_sec) * 1000000000LL + ts.tv_nsec;
    }
  }
  if (frame->stamp_ns == 0) {
    frame->stamp_ns = RealtimeNowNs();
  }
  return true;
}

bool SocketCanSource::Write(const CanFrame & frame, std::string * error)
{
  can_frame cf{};
  cf.can_id = frame.id & CAN_SFF_MASK;
  cf.can_dlc = frame.dlc > 8 ? 8 : frame.dlc;
  std::memcpy(cf.data, frame.data.data(), cf.can_dlc);
  const ssize_t n = write(fd_, &cf, sizeof(cf));
  if (n != static_cast<ssize_t>(sizeof(cf))) {
    if (errno == ENOBUFS) {
      SetError(error, "CAN 发送队列已满：总线上可能没有其它节点应答，或波特率不一致");
    } else {
      SetError(error, ErrnoText("发送 CAN 帧失败"));
    }
    return false;
  }
  return true;
}

void SocketCanSource::HandleErrorFrame(uint32_t can_id, const uint8_t * data)
{
  (void)data;
  ++bus_errors_.error_frames;
  if (can_id & CAN_ERR_BUSOFF) {
    ++bus_errors_.bus_off;
  }
  if (can_id & CAN_ERR_CRTL) {
    ++bus_errors_.controller_problems;
  }
  if (can_id & CAN_ERR_ACK) {
    ++bus_errors_.no_ack;
  }
  if (can_id & CAN_ERR_PROT) {
    ++bus_errors_.protocol_violations;
  }
}

// ---------------- candump 回放 ----------------

bool ParseCandumpLine(const std::string & line, int64_t * stamp_ns, CanFrame * frame)
{
  const size_t lp = line.find('(');
  const size_t rp = line.find(')');
  if (lp == std::string::npos || rp == std::string::npos || rp < lp) {
    return false;
  }
  const std::string ts = line.substr(lp + 1, rp - lp - 1);
  const size_t dot = ts.find('.');
  if (dot == std::string::npos || dot == 0) {
    return false;
  }
  int64_t sec = 0;
  for (size_t i = 0; i < dot; ++i) {
    if (ts[i] < '0' || ts[i] > '9') {
      return false;
    }
    sec = sec * 10 + (ts[i] - '0');
  }
  int64_t frac_ns = 0;
  int digits = 0;
  for (size_t i = dot + 1; i < ts.size(); ++i) {
    if (ts[i] < '0' || ts[i] > '9') {
      return false;
    }
    if (digits < 9) {
      frac_ns = frac_ns * 10 + (ts[i] - '0');
      ++digits;
    }
  }
  for (; digits < 9; ++digits) {
    frac_ns *= 10;
  }

  // 第三个字段为 "ID#DATA"
  const size_t hash = line.find('#', rp);
  if (hash == std::string::npos) {
    return false;
  }
  size_t id_begin = line.rfind(' ', hash);
  if (id_begin == std::string::npos || id_begin < rp) {
    return false;
  }
  ++id_begin;
  const std::string id_str = line.substr(id_begin, hash - id_begin);
  if (id_str.size() != 3) {
    return false;   // 只处理 11 位标准帧
  }
  uint32_t id = 0;
  for (char c : id_str) {
    const int v = HexValue(c);
    if (v < 0) {
      return false;
    }
    id = (id << 4) | static_cast<uint32_t>(v);
  }
  if (id > 0x7FF) {
    return false;
  }

  CanFrame f;
  f.id = id;
  size_t pos = hash + 1;
  if (pos < line.size() && (line[pos] == 'R' || line[pos] == '#')) {
    return false;   // 远程帧或 CAN FD
  }
  uint8_t dlc = 0;
  while (pos + 1 < line.size() && HexValue(line[pos]) >= 0 && HexValue(line[pos + 1]) >= 0) {
    if (dlc >= 8) {
      return false;
    }
    f.data[dlc++] = static_cast<uint8_t>(HexValue(line[pos]) * 16 + HexValue(line[pos + 1]));
    pos += 2;
  }
  f.dlc = dlc;
  *stamp_ns = sec * 1000000000LL + frac_ns;
  *frame = f;
  return true;
}

bool CandumpReplaySource::Open(const std::string & path, bool loop, std::string * error)
{
  path_ = path;
  loop_ = loop;
  std::ifstream in(path);
  if (!in) {
    SetError(error, "无法打开回放文件: " + path);
    return false;
  }
  std::string line;
  while (std::getline(in, line)) {
    Entry e{};
    if (ParseCandumpLine(line, &e.log_stamp_ns, &e.frame)) {
      entries_.push_back(e);
    }
  }
  if (entries_.empty()) {
    SetError(error, "回放文件中没有有效的 CAN 帧: " + path);
    return false;
  }
  next_ = 0;
  base_wall_ns_ = 0;
  return true;
}

bool CandumpReplaySource::Read(CanFrame * frame, int timeout_ms, std::string * error)
{
  if (next_ >= entries_.size()) {
    if (!loop_) {
      SetError(error, "回放结束");
      return false;
    }
    next_ = 0;
    base_wall_ns_ = 0;
  }
  const int64_t now = RealtimeNowNs();
  if (base_wall_ns_ == 0) {
    base_wall_ns_ = now;
    base_log_ns_ = entries_[next_].log_stamp_ns;
  }
  const Entry & e = entries_[next_];
  const int64_t target = base_wall_ns_ + (e.log_stamp_ns - base_log_ns_);
  const int64_t wait_ns = target - now;
  if (wait_ns > static_cast<int64_t>(timeout_ms) * 1000000LL) {
    std::this_thread::sleep_for(std::chrono::milliseconds(timeout_ms));
    return false;
  }
  if (wait_ns > 0) {
    std::this_thread::sleep_for(std::chrono::nanoseconds(wait_ns));
  }
  *frame = e.frame;
  frame->stamp_ns = target;
  ++next_;
  return true;
}

bool CandumpReplaySource::Write(const CanFrame & frame, std::string * error)
{
  (void)frame;
  SetError(error, "回放模式不支持发送");
  return false;
}

}  // namespace dozer_radar_sr73f
