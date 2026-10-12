// SR73F 毫米波雷达驱动节点：从 SocketCAN（或 candump 回放）读取多台雷达报文，
// 按雷达发布目标列表、点云、状态与诊断信息。

#include <algorithm>
#include <array>
#include <atomic>
#include <cstring>
#include <deque>
#include <memory>
#include <mutex>
#include <set>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "diagnostic_msgs/msg/diagnostic_array.hpp"
#include "dozer_msgs/msg/radar_object_array.hpp"
#include "dozer_msgs/msg/radar_status.hpp"
#include "rclcpp/rclcpp.hpp"
#include "rclcpp_components/register_node_macro.hpp"
#include "sensor_msgs/msg/point_cloud2.hpp"
#include "sensor_msgs/msg/point_field.hpp"

#include "dozer_radar_sr73f/can_frame_source.hpp"
#include "dozer_radar_sr73f/object_list_assembler.hpp"
#include "dozer_radar_sr73f/sr73f_protocol.hpp"

namespace dozer_radar_sr73f
{

namespace
{

using diagnostic_msgs::msg::DiagnosticStatus;

constexpr int64_t kStateWindowNs = 5000000000LL;

builtin_interfaces::msg::Time ToRosTime(int64_t ns)
{
  builtin_interfaces::msg::Time t;
  t.sec = static_cast<int32_t>(ns / 1000000000LL);
  t.nanosec = static_cast<uint32_t>(ns % 1000000000LL);
  return t;
}

diagnostic_msgs::msg::KeyValue KV(const std::string & key, const std::string & value)
{
  diagnostic_msgs::msg::KeyValue kv;
  kv.key = key;
  kv.value = value;
  return kv;
}

template<typename T>
std::string Str(T v)
{
  std::ostringstream os;
  os << v;
  return os.str();
}

}  // namespace

class RadarSr73fNode : public rclcpp::Node
{
public:
  explicit RadarSr73fNode(const rclcpp::NodeOptions & options)
  : Node("radar_sr73f", options)
  {
    LoadParameters();
    diag_pub_ = create_publisher<diagnostic_msgs::msg::DiagnosticArray>("/diagnostics", 10);
    last_diag_ns_ = RealtimeNowNs();
    diag_timer_ = create_wall_timer(std::chrono::seconds(1), [this]() {OnDiagnosticsTimer();});
    running_ = true;
    rx_thread_ = std::thread([this]() {RxLoop();});
  }

  ~RadarSr73fNode() override
  {
    running_ = false;
    if (rx_thread_.joinable()) {
      rx_thread_.join();
    }
  }

private:
  struct Radar
  {
    std::string name;
    uint8_t sensor_id = 0;
    std::string frame_id;
    std::unique_ptr<ObjectListAssembler> assembler;
    rclcpp::Publisher<dozer_msgs::msg::RadarObjectArray>::SharedPtr objects_pub;
    rclcpp::Publisher<sensor_msgs::msg::PointCloud2>::SharedPtr points_pub;
    rclcpp::Publisher<dozer_msgs::msg::RadarStatus>::SharedPtr status_pub;

    // 以下字段由 mutex_ 保护
    int64_t last_frame_ns = 0;
    bool has_state = false;
    RadarState state;
    bool has_version = false;
    SoftwareVersion version;
    int64_t last_region_ns = 0;
    RegionState region;
    uint8_t last_nof = 0;
    uint64_t bad_frames = 0;
    uint64_t window_cycles = 0;
    std::deque<int64_t> state_stamps;   // 最近 5 秒内 0x201 的接收时间
    AssemblerStats last_reported;
  };

  void LoadParameters()
  {
    can_interface_ = declare_parameter<std::string>("can_interface", "can0");
    replay_file_ = declare_parameter<std::string>("replay_file", "");
    replay_loop_ = declare_parameter<bool>("replay_loop", true);
    flip_lateral_ = declare_parameter<bool>("flip_lateral", true);
    publish_pointcloud_ = declare_parameter<bool>("publish_pointcloud", true);
    offline_timeout_ns_ = declare_parameter<int64_t>("offline_timeout_ms", 500) * 1000000LL;
    const int64_t cycle_timeout_ns = declare_parameter<int64_t>("cycle_timeout_ms", 100) * 1000000LL;
    const auto names = declare_parameter<std::vector<std::string>>(
      "radar_names", std::vector<std::string>{"front"});

    for (size_t i = 0; i < names.size(); ++i) {
      const std::string & name = names[i];
      const int64_t sid = declare_parameter<int64_t>(name + ".sensor_id", static_cast<int64_t>(i));
      const std::string frame_id = declare_parameter<std::string>(name + ".frame_id", "radar_" + name);
      if (sid < 0 || sid > kMaxSensorId) {
        throw std::invalid_argument("雷达 " + name + " 的 sensor_id 必须在 0-7 之间");
      }
      if (by_sensor_id_[sid] != nullptr) {
        throw std::invalid_argument("sensor_id " + Str(sid) + " 被多台雷达重复使用");
      }
      auto radar = std::make_unique<Radar>();
      radar->name = name;
      radar->sensor_id = static_cast<uint8_t>(sid);
      radar->frame_id = frame_id;
      const std::string prefix = "radar/" + name;
      radar->objects_pub = create_publisher<dozer_msgs::msg::RadarObjectArray>(
        prefix + "/objects", rclcpp::SensorDataQoS());
      radar->points_pub = create_publisher<sensor_msgs::msg::PointCloud2>(
        prefix + "/points", rclcpp::SensorDataQoS());
      radar->status_pub = create_publisher<dozer_msgs::msg::RadarStatus>(
        prefix + "/status", rclcpp::QoS(10).reliable());
      Radar * raw = radar.get();
      radar->assembler = std::make_unique<ObjectListAssembler>(
        radar->sensor_id, cycle_timeout_ns,
        [this, raw](const ObjectCycle & cycle) {PublishCycle(raw, cycle);});
      by_sensor_id_[sid] = raw;
      RCLCPP_INFO(get_logger(), "雷达 %s: SensorID=%ld, frame_id=%s", name.c_str(), static_cast<long>(sid),
        frame_id.c_str());
      radars_.push_back(std::move(radar));
    }
    if (radars_.empty()) {
      throw std::invalid_argument("radar_names 不能为空");
    }
  }

  bool OpenSource()
  {
    std::string error;
    if (!replay_file_.empty()) {
      auto src = std::make_unique<CandumpReplaySource>();
      if (!src->Open(replay_file_, replay_loop_, &error)) {
        RCLCPP_ERROR(get_logger(), "%s", error.c_str());
        return false;
      }
      std::lock_guard<std::mutex> lock(mutex_);
      source_ = std::move(src);
    } else {
      auto src = std::make_unique<SocketCanSource>();
      if (!src->Open(can_interface_, &error)) {
        RCLCPP_ERROR_THROTTLE(get_logger(), *get_clock(), 10000, "%s（2 秒后重试）", error.c_str());
        return false;
      }
      std::lock_guard<std::mutex> lock(mutex_);
      source_ = std::move(src);
    }
    RCLCPP_INFO(get_logger(), "数据源: %s", source_->description().c_str());
    return true;
  }

  void RxLoop()
  {
    while (running_ && !source_) {
      if (!OpenSource()) {
        if (!replay_file_.empty()) {
          return;
        }
        for (int i = 0; i < 20 && running_; ++i) {
          std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
      }
    }
    while (running_) {
      CanFrame frame;
      std::string error;
      const bool ok = source_->Read(&frame, 20, &error);
      const int64_t now = RealtimeNowNs();
      {
        std::lock_guard<std::mutex> lock(mutex_);
        bus_errors_ = source_->bus_errors();
        if (ok) {
          HandleFrameLocked(frame);
        } else if (!error.empty()) {
          source_error_ = error;
        }
        for (auto & r : radars_) {
          r->assembler->Poll(now);
        }
      }
      if (!ok && !error.empty()) {
        if (!replay_file_.empty()) {
          RCLCPP_INFO(get_logger(), "%s", error.c_str());
          return;
        }
        RCLCPP_ERROR_THROTTLE(get_logger(), *get_clock(), 5000, "%s", error.c_str());
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
      }
    }
  }

  void HandleFrameLocked(const CanFrame & frame)
  {
    ++total_frames_;
    MessageType type;
    uint8_t sid = 0;
    if (!ParseMessageId(frame.id, &type, &sid)) {
      ++other_frames_;
      return;
    }
    Radar * r = by_sensor_id_[sid];
    if (r == nullptr) {
      if (type != MessageType::kRadarCfg && type != MessageType::kRegionCfg) {
        if (unconfigured_ids_.insert(sid).second) {
          RCLCPP_WARN(get_logger(), "收到未配置的 SensorID=%u 的雷达报文（%s），请检查雷达 ID 或参数",
            sid, FormatFrame(frame).c_str());
        }
      }
      ++other_frames_;
      return;
    }
    r->last_frame_ns = frame.stamp_ns;
    switch (type) {
      case MessageType::kObjectStatus: {
          ObjectListStatus status;
          if (!DecodeObjectListStatus(frame, &status)) {
            ++r->bad_frames;
            return;
          }
          r->last_nof = status.nof_objects;
          r->assembler->OnStatus(status, frame.stamp_ns);
          break;
        }
      case MessageType::kObjectGeneral: {
          Object object;
          if (!DecodeObject(frame, &object)) {
            ++r->bad_frames;
            return;
          }
          r->assembler->OnObject(object);
          break;
        }
      case MessageType::kRadarState: {
          if (!DecodeRadarState(frame, &r->state)) {
            ++r->bad_frames;
            return;
          }
          r->has_state = true;
          r->state_stamps.push_back(frame.stamp_ns);
          while (!r->state_stamps.empty() &&
            frame.stamp_ns - r->state_stamps.front() > kStateWindowNs)
          {
            r->state_stamps.pop_front();
          }
          break;
        }
      case MessageType::kRegionState: {
          if (!DecodeRegionState(frame, &r->region)) {
            ++r->bad_frames;
            return;
          }
          r->last_region_ns = frame.stamp_ns;
          break;
        }
      case MessageType::kVersion: {
          if (DecodeSoftwareVersion(frame, &r->version)) {
            r->has_version = true;
          }
          break;
        }
      case MessageType::kRadarCfg:
      case MessageType::kRegionCfg:
        // 其它节点（如配置工具）下发的配置报文，忽略
        break;
    }
  }

  // 在 RX 线程中、持有 mutex_ 时由 assembler 回调
  void PublishCycle(Radar * r, const ObjectCycle & cycle)
  {
    ++r->window_cycles;
    const float lat_sign = flip_lateral_ ? -1.0f : 1.0f;

    auto msg = std::make_unique<dozer_msgs::msg::RadarObjectArray>();
    msg->header.stamp = ToRosTime(cycle.stamp_ns);
    msg->header.frame_id = r->frame_id;
    msg->sensor_id = r->sensor_id;
    msg->meas_count = cycle.status.meas_count;
    msg->nof_objects_reported = cycle.status.nof_objects;
    msg->objects.reserve(cycle.objects.size());
    for (const Object & o : cycle.objects) {
      dozer_msgs::msg::RadarObject ro;
      ro.id = o.id;
      ro.x = o.dist_long;
      ro.y = lat_sign * o.dist_lat;
      ro.vx = o.vrel_long;
      ro.vy = lat_sign * o.vrel_lat;
      ro.rcs = o.rcs;
      ro.dyn_prop = o.dyn_prop;
      msg->objects.push_back(ro);
    }

    if (publish_pointcloud_ && r->points_pub->get_subscription_count() > 0) {
      r->points_pub->publish(MakePointCloud(*msg));
    }
    r->objects_pub->publish(std::move(msg));
  }

  static sensor_msgs::msg::PointCloud2 MakePointCloud(const dozer_msgs::msg::RadarObjectArray & in)
  {
    static const char * kFields[] = {"x", "y", "z", "vx", "vy", "rcs", "id"};
    constexpr uint32_t kNumFields = 7;
    sensor_msgs::msg::PointCloud2 pc;
    pc.header = in.header;
    pc.height = 1;
    pc.width = static_cast<uint32_t>(in.objects.size());
    for (uint32_t i = 0; i < kNumFields; ++i) {
      sensor_msgs::msg::PointField f;
      f.name = kFields[i];
      f.offset = i * 4;
      f.datatype = sensor_msgs::msg::PointField::FLOAT32;
      f.count = 1;
      pc.fields.push_back(f);
    }
    pc.is_bigendian = false;
    pc.point_step = kNumFields * 4;
    pc.row_step = pc.point_step * pc.width;
    pc.is_dense = true;
    pc.data.resize(pc.row_step);
    float * p = reinterpret_cast<float *>(pc.data.data());
    for (const auto & o : in.objects) {
      const float v[kNumFields] = {o.x, o.y, 0.0f, o.vx, o.vy, o.rcs, static_cast<float>(o.id)};
      std::memcpy(p, v, sizeof(v));
      p += kNumFields;
    }
    return pc;
  }

  void OnDiagnosticsTimer()
  {
    const int64_t now = RealtimeNowNs();
    diagnostic_msgs::msg::DiagnosticArray diag;
    diag.header.stamp = ToRosTime(now);
    std::vector<std::unique_ptr<dozer_msgs::msg::RadarStatus>> statuses;

    {
      std::lock_guard<std::mutex> lock(mutex_);
      const double dt = std::max(1e-3, static_cast<double>(now - last_diag_ns_) / 1e9);
      last_diag_ns_ = now;

      for (auto & rp : radars_) {
        Radar & r = *rp;
        const bool online = r.last_frame_ns != 0 && now - r.last_frame_ns < offline_timeout_ns_;
        const double rate = static_cast<double>(r.window_cycles) / dt;
        // 单台雷达约 1 Hz 发送 0x201；5 秒内超过 7 帧说明有多台雷达使用同一 SensorID
        size_t recent_states = 0;
        for (int64_t t : r.state_stamps) {
          if (now - t <= kStateWindowNs) {
            ++recent_states;
          }
        }
        const bool region_active = r.last_region_ns != 0 && now - r.last_region_ns < 3000000000LL;
        const AssemblerStats & st = r.assembler->stats();
        const uint64_t new_incomplete = st.incomplete_cycles - r.last_reported.incomplete_cycles;

        auto status = std::make_unique<dozer_msgs::msg::RadarStatus>();
        status->header.stamp = ToRosTime(now);
        status->header.frame_id = r.frame_id;
        status->sensor_id = r.sensor_id;
        status->online = online;
        status->cycle_rate_hz = static_cast<float>(rate);
        if (r.has_version) {
          status->firmware_version = Str(int(r.version.major)) + "." + Str(int(r.version.minor)) +
            "." + Str(int(r.version.patch));
        }
        if (r.has_state) {
          status->max_distance_m = r.state.max_distance_m;
          status->sort_index = r.state.sort_index;
          status->radar_power = r.state.radar_power;
          status->output_type = r.state.output_type;
          status->rcs_threshold = r.state.rcs_threshold;
          status->can_baudrate = r.state.can_baudrate;
          status->motion_rx_state = r.state.motion_rx_state;
          status->nvm_read_ok = r.state.nvm_read_ok;
          status->nvm_write_ok = r.state.nvm_write_ok;
        }
        status->region_active = region_active;
        status->max_output_number = region_active ? r.region.max_output_number : 0;

        DiagnosticStatus ds;
        ds.name = std::string(get_name()) + ": " + r.name;
        ds.hardware_id = "sr73f_id" + Str(int(r.sensor_id));
        ds.level = DiagnosticStatus::OK;
        ds.message = "正常";
        if (!online) {
          ds.level = DiagnosticStatus::ERROR;
          ds.message = r.last_frame_ns == 0 ? "未收到报文" : "离线";
        } else if (recent_states > 7) {
          ds.level = DiagnosticStatus::WARN;
          ds.message = "0x201 状态报文过多，可能有多台雷达使用同一 SensorID";
        } else if (r.has_state && r.state.sensor_id != r.sensor_id) {
          ds.level = DiagnosticStatus::WARN;
          ds.message = "雷达上报的 SensorID 与配置不一致";
        } else if (rate < 20.0) {
          ds.level = DiagnosticStatus::WARN;
          ds.message = "目标周期频率偏低";
        } else if (new_incomplete > 0) {
          ds.level = DiagnosticStatus::WARN;
          ds.message = "存在不完整周期（丢帧），检查总线负载与接线";
        }
        ds.values.push_back(KV("cycle_rate_hz", Str(rate)));
        ds.values.push_back(KV("last_nof_objects", Str(int(r.last_nof))));
        ds.values.push_back(KV("complete_cycles", Str(st.complete_cycles)));
        ds.values.push_back(KV("incomplete_cycles", Str(st.incomplete_cycles)));
        ds.values.push_back(KV("orphan_objects", Str(st.orphan_objects)));
        ds.values.push_back(KV("meas_count_gaps", Str(st.meas_count_gaps)));
        ds.values.push_back(KV("bad_frames", Str(r.bad_frames)));
        ds.values.push_back(KV("firmware", status->firmware_version));
        if (r.has_state) {
          ds.values.push_back(KV("reported_sensor_id", Str(int(r.state.sensor_id))));
          ds.values.push_back(KV("can_baudrate", BaudRateName(r.state.can_baudrate)));
        }
        ds.values.push_back(KV("region_active", region_active ? "true" : "false"));
        if (region_active) {
          ds.values.push_back(KV("max_output_number", Str(int(r.region.max_output_number))));
        }
        diag.status.push_back(ds);
        statuses.push_back(std::move(status));

        r.window_cycles = 0;
        r.last_reported = st;
      }

      DiagnosticStatus bus;
      bus.name = std::string(get_name()) + ": bus";
      bus.hardware_id = source_ ? source_->description() : can_interface_;
      bus.level = DiagnosticStatus::OK;
      bus.message = "正常";
      const uint64_t new_errors = bus_errors_.error_frames - last_bus_errors_.error_frames;
      if (!source_) {
        bus.level = DiagnosticStatus::ERROR;
        bus.message = "CAN 接口未打开";
      } else if (bus_errors_.bus_off > last_bus_errors_.bus_off) {
        bus.level = DiagnosticStatus::ERROR;
        bus.message = "CAN 总线 bus-off";
      } else if (new_errors > 0) {
        bus.level = DiagnosticStatus::WARN;
        bus.message = "CAN 总线出现错误帧，检查波特率、终端电阻与接线";
      } else if (!unconfigured_ids_.empty()) {
        bus.level = DiagnosticStatus::WARN;
        bus.message = "总线上存在未配置的雷达 SensorID";
      }
      std::string ids;
      for (uint8_t id : unconfigured_ids_) {
        ids += (ids.empty() ? "" : ",") + Str(int(id));
      }
      bus.values.push_back(KV("total_frames", Str(total_frames_)));
      bus.values.push_back(KV("other_frames", Str(other_frames_)));
      bus.values.push_back(KV("unconfigured_sensor_ids", ids));
      bus.values.push_back(KV("error_frames", Str(bus_errors_.error_frames)));
      bus.values.push_back(KV("bus_off", Str(bus_errors_.bus_off)));
      bus.values.push_back(KV("no_ack", Str(bus_errors_.no_ack)));
      bus.values.push_back(KV("protocol_violations", Str(bus_errors_.protocol_violations)));
      bus.values.push_back(KV("controller_problems", Str(bus_errors_.controller_problems)));
      bus.values.push_back(KV("last_source_error", source_error_));
      diag.status.push_back(bus);
      last_bus_errors_ = bus_errors_;
    }

    for (size_t i = 0; i < statuses.size(); ++i) {
      radars_[i]->status_pub->publish(std::move(statuses[i]));
    }
    diag_pub_->publish(diag);
  }

  // 参数
  std::string can_interface_;
  std::string replay_file_;
  bool replay_loop_ = true;
  bool flip_lateral_ = true;
  bool publish_pointcloud_ = true;
  int64_t offline_timeout_ns_ = 500000000LL;

  std::vector<std::unique_ptr<Radar>> radars_;
  std::array<Radar *, kMaxSensorId + 1> by_sensor_id_{};
  std::unique_ptr<CanFrameSource> source_;
  std::thread rx_thread_;
  std::atomic<bool> running_{false};
  rclcpp::Publisher<diagnostic_msgs::msg::DiagnosticArray>::SharedPtr diag_pub_;
  rclcpp::TimerBase::SharedPtr diag_timer_;

  // 以下字段由 mutex_ 保护
  std::mutex mutex_;
  uint64_t total_frames_ = 0;
  uint64_t other_frames_ = 0;
  std::set<uint8_t> unconfigured_ids_;
  BusErrorStats bus_errors_;
  BusErrorStats last_bus_errors_;
  std::string source_error_;
  int64_t last_diag_ns_ = 0;
};

}  // namespace dozer_radar_sr73f

RCLCPP_COMPONENTS_REGISTER_NODE(dozer_radar_sr73f::RadarSr73fNode)
