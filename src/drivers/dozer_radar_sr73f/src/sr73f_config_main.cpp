// SR73F 雷达配置与排查工具（不依赖 ROS）。
// 会写入雷达的命令（set-*）只在显式执行时下发，驱动节点启动时不会自动配置雷达。

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <map>
#include <string>
#include <vector>

#include "dozer_radar_sr73f/can_frame_source.hpp"
#include "dozer_radar_sr73f/sr73f_protocol.hpp"

using namespace dozer_radar_sr73f;

namespace
{

struct Args
{
  std::string command;
  std::string interface = "can0";
  double seconds = 3.0;
  bool dry_run = false;
  bool force = false;
  bool store = true;
  int id = -1;
  int from = -1;
  int to = -1;
  std::string baud;
  int max_objects = -1;
  bool has_p1 = false;
  bool has_p2 = false;
  float p1_long = 0, p1_lat = 0, p2_long = 0, p2_lat = 0;
  bool inactive = false;
};

void PrintUsage()
{
  std::printf(
    "用法: sr73f_config [-i can0] <命令> [选项]\n"
    "\n"
    "排查命令（只读）:\n"
    "  scan [-t 秒]                      列出总线上的雷达：ID、固件、波特率、区域、目标频率\n"
    "  dump [-t 秒] [--id N]             打印解码后的报文\n"
    "\n"
    "配置命令（写入雷达，默认保存到 NVM）:\n"
    "  set-id --from A --to B            修改 SensorID。总线上只能有一台使用 ID A 的雷达\n"
    "  set-baud --id A --baud 500k|250k|1m\n"
    "                                    修改雷达波特率，之后需同步修改 CAN 接口波特率\n"
    "  set-region --id A --max-objects N --p1 纵向,横向 --p2 纵向,横向 [--inactive]\n"
    "                                    设置矩形框与框内最大目标数（0-63）\n"
    "                                    坐标为雷达原始坐标：横向右正；点 1 为右下角，点 2 为左上角\n"
    "                                    例：前方 40 m、左右各 20 m、最多 30 个目标：\n"
    "                                    --max-objects 30 --p1 0,20 --p2 40,-20\n"
    "  disable-region --id A             关闭区域过滤，恢复输出全部目标\n"
    "\n"
    "通用选项:\n"
    "  -i, --interface IF   CAN 接口，默认 can0\n"
    "  --dry-run            只打印将要发送的报文，不发送\n"
    "  --no-store           不写入 NVM（掉电后恢复原配置）\n"
    "  --force              跳过发送前的安全检查\n");
}

bool ParsePoint(const char * s, float * lng, float * lat)
{
  return std::sscanf(s, "%f,%f", lng, lat) == 2;
}

bool ParseArgs(int argc, char ** argv, Args * a)
{
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    auto next = [&](const char * name) -> const char * {
        if (i + 1 >= argc) {
          std::fprintf(stderr, "选项 %s 缺少参数\n", name);
          return nullptr;
        }
        return argv[++i];
      };
    const char * v = nullptr;
    if (arg == "-h" || arg == "--help") {
      return false;
    } else if (arg == "-i" || arg == "--interface") {
      if (!(v = next("-i"))) {return false;}
      a->interface = v;
    } else if (arg == "-t") {
      if (!(v = next("-t"))) {return false;}
      a->seconds = std::atof(v);
    } else if (arg == "--dry-run") {
      a->dry_run = true;
    } else if (arg == "--force") {
      a->force = true;
    } else if (arg == "--no-store") {
      a->store = false;
    } else if (arg == "--inactive") {
      a->inactive = true;
    } else if (arg == "--id") {
      if (!(v = next("--id"))) {return false;}
      a->id = std::atoi(v);
    } else if (arg == "--from") {
      if (!(v = next("--from"))) {return false;}
      a->from = std::atoi(v);
    } else if (arg == "--to") {
      if (!(v = next("--to"))) {return false;}
      a->to = std::atoi(v);
    } else if (arg == "--baud") {
      if (!(v = next("--baud"))) {return false;}
      a->baud = v;
    } else if (arg == "--max-objects") {
      if (!(v = next("--max-objects"))) {return false;}
      a->max_objects = std::atoi(v);
    } else if (arg == "--p1") {
      if (!(v = next("--p1")) || !ParsePoint(v, &a->p1_long, &a->p1_lat)) {return false;}
      a->has_p1 = true;
    } else if (arg == "--p2") {
      if (!(v = next("--p2")) || !ParsePoint(v, &a->p2_long, &a->p2_lat)) {return false;}
      a->has_p2 = true;
    } else if (a->command.empty() && arg[0] != '-') {
      a->command = arg;
    } else {
      std::fprintf(stderr, "未知参数: %s\n", arg.c_str());
      return false;
    }
  }
  return !a->command.empty();
}

// 在 seconds 秒内读取报文并回调；返回 false 表示读取出错。
bool Listen(CanFrameSource * src, double seconds, const std::function<void(const CanFrame &)> & cb)
{
  const auto deadline = std::chrono::steady_clock::now() +
    std::chrono::milliseconds(static_cast<int64_t>(seconds * 1000));
  while (std::chrono::steady_clock::now() < deadline) {
    CanFrame f;
    std::string error;
    if (src->Read(&f, 50, &error)) {
      cb(f);
    } else if (!error.empty()) {
      std::fprintf(stderr, "读取失败: %s\n", error.c_str());
      return false;
    }
  }
  return true;
}

struct RadarSeen
{
  int state_msgs = 0;
  int status_msgs = 0;
  int object_msgs = 0;
  int region_msgs = 0;
  bool has_state = false;
  RadarState state;
  bool has_version = false;
  SoftwareVersion version;
  RegionState region;
  int last_nof = -1;
};

std::map<int, RadarSeen> Survey(CanFrameSource * src, double seconds, int * other)
{
  std::map<int, RadarSeen> seen;
  *other = 0;
  Listen(src, seconds, [&](const CanFrame & f) {
      MessageType type;
      uint8_t sid;
      if (!ParseMessageId(f.id, &type, &sid)) {
        ++*other;
        return;
      }
      RadarSeen & r = seen[sid];
      switch (type) {
        case MessageType::kRadarState:
          ++r.state_msgs;
          r.has_state = DecodeRadarState(f, &r.state) || r.has_state;
          break;
        case MessageType::kObjectStatus: {
            ++r.status_msgs;
            ObjectListStatus s;
            if (DecodeObjectListStatus(f, &s)) {
              r.last_nof = s.nof_objects;
            }
            break;
          }
        case MessageType::kObjectGeneral:
          ++r.object_msgs;
          break;
        case MessageType::kRegionState:
          ++r.region_msgs;
          DecodeRegionState(f, &r.region);
          break;
        case MessageType::kVersion:
          r.has_version = DecodeSoftwareVersion(f, &r.version) || r.has_version;
          break;
        default:
          break;
      }
    });
  // 只保留确实有雷达输出的 ID（排除仅出现配置报文的情况）
  for (auto it = seen.begin(); it != seen.end(); ) {
    const RadarSeen & r = it->second;
    if (r.state_msgs + r.status_msgs + r.object_msgs + r.region_msgs == 0 && !r.has_version) {
      it = seen.erase(it);
    } else {
      ++it;
    }
  }
  return seen;
}

void PrintBusErrors(const CanFrameSource & src)
{
  const BusErrorStats e = src.bus_errors();
  if (e.error_frames == 0) {
    return;
  }
  std::printf("总线错误帧: %llu（bus-off %llu，无应答 %llu，协议错误 %llu，控制器 %llu）\n",
    (unsigned long long)e.error_frames, (unsigned long long)e.bus_off,
    (unsigned long long)e.no_ack, (unsigned long long)e.protocol_violations,
    (unsigned long long)e.controller_problems);
  std::printf("  提示：协议错误多见于波特率不一致或终端电阻缺失\n");
}

int CmdScan(CanFrameSource * src, const Args & a)
{
  std::printf("监听 %s %.1f 秒...\n", src->description().c_str(), a.seconds);
  int other = 0;
  const auto seen = Survey(src, a.seconds, &other);
  if (seen.empty()) {
    std::printf("未发现 SR73F 雷达报文（其它报文 %d 帧）。\n", other);
    std::printf("请检查：雷达供电（9-24V）、CAN_H/CAN_L 接线、终端电阻、"
      "雷达与 %s 的波特率是否一致（ip -details link show %s）。\n",
      a.interface.c_str(), a.interface.c_str());
    PrintBusErrors(*src);
    return 1;
  }
  std::printf("\n%-4s %-9s %-6s %-7s %-9s %-9s %-14s %s\n",
    "ID", "固件", "波特率", "上报ID", "周期/s", "目标数", "区域", "备注");
  for (const auto & kv : seen) {
    const RadarSeen & r = kv.second;
    char fw[32] = "-";
    if (r.has_version) {
      std::snprintf(fw, sizeof(fw), "%u.%u.%u", r.version.major, r.version.minor, r.version.patch);
    }
    char region[32] = "未启用";
    if (r.region_msgs > 0) {
      std::snprintf(region, sizeof(region), "启用,max=%u", r.region.max_output_number);
    }
    std::string note;
    if (r.state_msgs / a.seconds > 1.6) {
      note = "0x201 过多，可能有多台雷达使用同一 ID";
    }
    std::printf("%-4d %-9s %-6s %-7s %-9.1f %-9s %-14s %s\n",
      kv.first, fw, r.has_state ? BaudRateName(r.state.can_baudrate) : "-",
      r.has_state ? std::to_string(r.state.sensor_id).c_str() : "-",
      r.status_msgs / a.seconds,
      r.last_nof >= 0 ? std::to_string(r.last_nof).c_str() : "-", region, note.c_str());
  }
  std::printf("\n其它报文: %d 帧\n", other);
  PrintBusErrors(*src);
  return 0;
}

int CmdDump(CanFrameSource * src, const Args & a)
{
  Listen(src, a.seconds, [&](const CanFrame & f) {
      MessageType type;
      uint8_t sid;
      if (!ParseMessageId(f.id, &type, &sid)) {
        if (a.id < 0) {
          std::printf("%.6f %s 非 SR73F 报文\n", f.stamp_ns / 1e9, FormatFrame(f).c_str());
        }
        return;
      }
      if (a.id >= 0 && sid != a.id) {
        return;
      }
      std::printf("%.6f %s id=%u %-13s ", f.stamp_ns / 1e9, FormatFrame(f).c_str(), sid,
        MessageTypeName(type));
      switch (type) {
        case MessageType::kObjectStatus: {
            ObjectListStatus s;
            if (DecodeObjectListStatus(f, &s)) {
              std::printf("nof=%u meas=%u", s.nof_objects, s.meas_count);
            }
            break;
          }
        case MessageType::kObjectGeneral: {
            Object o;
            if (DecodeObject(f, &o)) {
              std::printf("obj=%u long=%.1f lat=%.1f(右正) vlong=%.2f vlat=%.2f rcs=%.1f",
                o.id, o.dist_long, o.dist_lat, o.vrel_long, o.vrel_lat, o.rcs);
            }
            break;
          }
        case MessageType::kRadarState: {
            RadarState s;
            if (DecodeRadarState(f, &s)) {
              std::printf("sensor_id=%u baud=%s sort=%u rcs_thr=%u nvm_r=%d nvm_w=%d",
                s.sensor_id, BaudRateName(s.can_baudrate), s.sort_index, s.rcs_threshold,
                s.nvm_read_ok, s.nvm_write_ok);
            }
            break;
          }
        case MessageType::kRegionState: {
            RegionState s;
            if (DecodeRegionState(f, &s)) {
              std::printf("max=%u region=%u p1=(%.1f,%.1f) p2=(%.1f,%.1f)", s.max_output_number,
                s.region_id, s.region.point1_long, s.region.point1_lat, s.region.point2_long,
                s.region.point2_lat);
            }
            break;
          }
        case MessageType::kVersion: {
            SoftwareVersion v;
            if (DecodeSoftwareVersion(f, &v)) {
              std::printf("version=%u.%u.%u", v.major, v.minor, v.patch);
            }
            break;
          }
        default:
          break;
      }
      std::printf("\n");
    });
  PrintBusErrors(*src);
  return 0;
}

bool Send(CanFrameSource * src, const CanFrame & f, bool dry_run)
{
  std::printf("%s %s\n", dry_run ? "[dry-run] 将发送:" : "发送:", FormatFrame(f).c_str());
  if (dry_run) {
    return true;
  }
  std::string error;
  if (!src->Write(f, &error)) {
    std::fprintf(stderr, "发送失败: %s\n", error.c_str());
    return false;
  }
  return true;
}

// 发送前检查目标雷达在线且没有 ID 冲突。
bool PreCheck(CanFrameSource * src, int sensor_id, const Args & a)
{
  if (a.force || a.dry_run) {
    return true;
  }
  std::printf("检查总线上的雷达（2.5 秒）...\n");
  int other = 0;
  const auto seen = Survey(src, 2.5, &other);
  auto it = seen.find(sensor_id);
  if (it == seen.end()) {
    std::fprintf(stderr, "未发现 SensorID=%d 的雷达。可用 scan 查看总线，或用 --force 跳过检查。\n",
      sensor_id);
    return false;
  }
  if (it->second.state_msgs > 4) {
    std::fprintf(stderr, "SensorID=%d 的 0x201 报文过多，可能有多台雷达使用同一 ID。"
      "请只保留一台接在总线上再配置（或用 --force）。\n", sensor_id);
    return false;
  }
  return true;
}

int CmdSetId(CanFrameSource * src, const Args & a)
{
  if (a.from < 0 || a.from > 7 || a.to < 0 || a.to > 7) {
    std::fprintf(stderr, "set-id 需要 --from 与 --to（0-7）\n");
    return 2;
  }
  if (!PreCheck(src, a.from, a)) {
    return 1;
  }
  if (!a.force && !a.dry_run) {
    int other = 0;
    if (Survey(src, 1.5, &other).count(a.to) != 0) {
      std::fprintf(stderr, "总线上已有 SensorID=%d 的雷达，修改后会冲突（或用 --force）。\n", a.to);
      return 1;
    }
  }
  RadarConfig cfg;
  cfg.sensor_id_valid = true;
  cfg.sensor_id = static_cast<uint8_t>(a.to);
  cfg.store_in_nvm = a.store;
  CanFrame f;
  std::string error;
  if (!EncodeRadarConfig(static_cast<uint8_t>(a.from), cfg, &f, &error)) {
    std::fprintf(stderr, "%s\n", error.c_str());
    return 2;
  }
  if (!Send(src, f, a.dry_run)) {
    return 1;
  }
  if (a.dry_run) {
    return 0;
  }
  std::printf("等待雷达以新 ID 上报 0x201...\n");
  bool confirmed = false;
  const uint32_t want = MessageId(MessageType::kRadarState, static_cast<uint8_t>(a.to));
  Listen(src, 3.0, [&](const CanFrame & r) {
      RadarState s;
      if (r.id == want && DecodeRadarState(r, &s) && s.sensor_id == a.to) {
        confirmed = true;
      }
    });
  if (confirmed) {
    std::printf("成功：雷达已使用 SensorID=%d%s。\n", a.to, a.store ? "（已保存到 NVM）" : "");
    return 0;
  }
  std::fprintf(stderr, "3 秒内未收到新 ID 的状态报文，请用 scan 确认。\n");
  return 1;
}

int CmdSetBaud(CanFrameSource * src, const Args & a)
{
  RadarConfig cfg;
  cfg.baudrate_valid = true;
  if (a.baud == "500k" || a.baud == "500K") {
    cfg.baudrate = BaudRate::k500K;
  } else if (a.baud == "250k" || a.baud == "250K") {
    cfg.baudrate = BaudRate::k250K;
  } else if (a.baud == "1m" || a.baud == "1M") {
    cfg.baudrate = BaudRate::k1M;
  } else {
    std::fprintf(stderr, "--baud 只能是 500k、250k 或 1m\n");
    return 2;
  }
  if (a.id < 0 || a.id > 7) {
    std::fprintf(stderr, "set-baud 需要 --id（0-7）\n");
    return 2;
  }
  if (!PreCheck(src, a.id, a)) {
    return 1;
  }
  cfg.store_in_nvm = a.store;
  CanFrame f;
  std::string error;
  if (!EncodeRadarConfig(static_cast<uint8_t>(a.id), cfg, &f, &error)) {
    std::fprintf(stderr, "%s\n", error.c_str());
    return 2;
  }
  if (!Send(src, f, a.dry_run)) {
    return 1;
  }
  if (!a.dry_run) {
    std::printf("已下发。雷达将切换到 %s，当前接口波特率下将收不到它的报文。\n"
      "所有雷达改完后修改接口波特率（需 sudo），再用 scan 确认，例如：\n"
      "  sudo ip link set %s down && sudo ip link set %s up type can bitrate %s restart-ms 100\n",
      a.baud.c_str(), a.interface.c_str(), a.interface.c_str(),
      cfg.baudrate == BaudRate::k1M ? "1000000" :
      (cfg.baudrate == BaudRate::k250K ? "250000" : "500000"));
  }
  return 0;
}

int CmdSetRegion(CanFrameSource * src, const Args & a, bool disable)
{
  if (a.id < 0 || a.id > 7) {
    std::fprintf(stderr, "需要 --id（0-7）\n");
    return 2;
  }
  RegionConfig cfg;
  if (disable) {
    // 关闭时仍需给出合法坐标，否则雷达不保存；使用默认 50 x 50 m 框
    cfg.activation = false;
    cfg.coordinates_valid = false;
    cfg.max_output_number = 63;
    cfg.region = Region{0.0f, 25.0f, 50.0f, -25.0f};
  } else {
    if (a.max_objects < 0 || a.max_objects > 63 || !a.has_p1 || !a.has_p2) {
      std::fprintf(stderr, "set-region 需要 --max-objects（0-63）、--p1、--p2\n");
      return 2;
    }
    cfg.activation = !a.inactive;
    cfg.coordinates_valid = true;
    cfg.max_output_number = static_cast<uint8_t>(a.max_objects);
    cfg.region = Region{a.p1_long, a.p1_lat, a.p2_long, a.p2_lat};
  }
  CanFrame f;
  std::string error;
  if (!EncodeRegionConfig(static_cast<uint8_t>(a.id), cfg, &f, &error)) {
    std::fprintf(stderr, "%s\n", error.c_str());
    return 2;
  }
  if (!PreCheck(src, a.id, a)) {
    return 1;
  }
  if (!Send(src, f, a.dry_run)) {
    return 1;
  }
  if (a.dry_run || disable || a.inactive) {
    return 0;
  }
  std::printf("等待 0x402 区域状态（约 1 Hz）...\n");
  bool confirmed = false;
  const uint32_t want = MessageId(MessageType::kRegionState, static_cast<uint8_t>(a.id));
  Listen(src, 2.5, [&](const CanFrame & r) {
      RegionState s;
      if (r.id == want && DecodeRegionState(r, &s) &&
      s.max_output_number == cfg.max_output_number)
      {
        confirmed = true;
      }
    });
  if (confirmed) {
    std::printf("成功：区域过滤已生效，最大目标数 %d。\n", a.max_objects);
    return 0;
  }
  std::fprintf(stderr, "2.5 秒内未收到匹配的 0x402，请用 dump --id %d 确认。\n", a.id);
  return 1;
}

// --dry-run 时代替真实 CAN 接口：不读不写，只打印将要发送的报文
class DryRunSource : public CanFrameSource
{
public:
  bool Read(CanFrame *, int, std::string *) override {return false;}
  bool Write(const CanFrame &, std::string *) override {return true;}
  std::string description() const override {return "dry-run";}
};

}  // namespace

int main(int argc, char ** argv)
{
  Args a;
  if (!ParseArgs(argc, argv, &a)) {
    PrintUsage();
    return 2;
  }
  const bool writes = a.command.rfind("set-", 0) == 0 || a.command == "disable-region";
  DryRunSource dry;
  SocketCanSource socket_src;
  CanFrameSource * src = &dry;
  if (!(a.dry_run && writes)) {
    std::string error;
    if (!socket_src.Open(a.interface, &error)) {
      std::fprintf(stderr, "%s\n", error.c_str());
      return 1;
    }
    src = &socket_src;
  }
  if (a.command == "scan") {
    return CmdScan(src, a);
  } else if (a.command == "dump") {
    return CmdDump(src, a);
  } else if (a.command == "set-id") {
    return CmdSetId(src, a);
  } else if (a.command == "set-baud") {
    return CmdSetBaud(src, a);
  } else if (a.command == "set-region") {
    return CmdSetRegion(src, a, false);
  } else if (a.command == "disable-region") {
    return CmdSetRegion(src, a, true);
  }
  std::fprintf(stderr, "未知命令: %s\n", a.command.c_str());
  PrintUsage();
  return 2;
}
