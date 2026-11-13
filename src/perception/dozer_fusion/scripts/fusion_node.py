#!/usr/bin/env python3
"""Classify radar snapshots using configured camera views."""

import math
import time
from collections import defaultdict, deque

import rclpy
from rclpy.node import Node
from rclpy.qos import qos_profile_sensor_data
from rclpy.time import Time
from tf2_ros import Buffer, TransformListener, TransformException
from dozer_msgs.msg import Detection2DArray, FusedDetection, FusedDetectionArray, RadarObjectArray

UNKNOWN_CLASS_ID = 4294967295


def seconds(stamp):
    return stamp.sec + stamp.nanosec * 1e-9


def rotate(q, p):
    u = (q.x, q.y, q.z)
    t = tuple(2 * (u[(i + 1) % 3] * p[(i + 2) % 3] -
                   u[(i + 2) % 3] * p[(i + 1) % 3]) for i in range(3))
    cross = tuple(u[(i + 1) % 3] * t[(i + 2) % 3] -
                  u[(i + 2) % 3] * t[(i + 1) % 3] for i in range(3))
    return tuple(p[i] + q.w * t[i] + cross[i] for i in range(3))


def transform_point(tf, p):
    rotated = rotate(tf.rotation, p)
    t = tf.translation
    return rotated[0] + t.x, rotated[1] + t.y, rotated[2] + t.z


def project_horizontal(point, camera):
    x, _, z = point
    if z <= 0 or not math.isfinite(x) or not math.isfinite(z):
        return None
    return (camera['fx'] * x / z + camera['cx']) / camera['width']


def associate(points, detections, camera):
    """Associate by horizontal image position; radar has no target height."""
    pairs = []
    for ri, point in enumerate(points):
        u = project_horizontal(point, camera)
        if u is None or not 0.0 <= u <= 1.0:
            continue
        for di, box in enumerate(detections):
            if box.w > 0 and box.x <= u <= box.x + box.w:
                center_x = box.x + box.w / 2
                distance = abs(u - center_x) / box.w
                pairs.append((distance, ri, di))
    result, used_radar, used_box = {}, set(), set()
    for _, ri, di in sorted(pairs):
        if ri not in used_radar and di not in used_box:
            result[ri] = detections[di]
            used_radar.add(ri)
            used_box.add(di)
    return result


class FusionNode(Node):
    def __init__(self):
        super().__init__('fusion_node')
        self.declare_parameter('radar_topics', ['/radar/front_left/objects',
                                               '/radar/front_right/objects',
                                               '/radar/rear/objects'])
        self.declare_parameter('camera_names', ['front_left', 'front_right', 'rear'])
        self.declare_parameter('radar_camera_pairs', [
            'radar_front_left:front_left,front_right,left',
            'radar_front_right:front_left,front_right,right',
            'radar_rear:rear,left,right'])
        self.declare_parameter('output_topic', '/perception/fused_detections')
        self.declare_parameter('output_frame', 'base_link')
        self.declare_parameter('max_age_sec', 0.12)
        self.declare_parameter('stale_sec', 0.5)
        self.declare_parameter('max_match_distance_m', 0.0)
        self.declare_parameter('publish_hz', 15.0)
        self.frame = self.get_parameter('output_frame').value
        self.max_age = float(self.get_parameter('max_age_sec').value)
        self.stale = float(self.get_parameter('stale_sec').value)
        self.max_match_distance = float(self.get_parameter('max_match_distance_m').value)
        hz = float(self.get_parameter('publish_hz').value)
        if min(self.max_age, self.stale, hz) <= 0 or self.max_match_distance < 0:
            raise ValueError('max_age_sec, stale_sec and publish_hz must be positive; '
                             'max_match_distance_m must be non-negative')
        self.cameras = {}
        for name in self.get_parameter('camera_names').value:
            for key, default in (('frame', name + '_optical'), ('detections_topic',
                                  '/perception/' + name + '/detections'),
                                 ('width', 0), ('height', 0), ('fx', 0.0),
                                 ('fy', 0.0), ('cx', 0.0), ('cy', 0.0)):
                self.declare_parameter(name + '.' + key, default)
            camera = {key: self.get_parameter(name + '.' + key).value for key in
                      ('frame', 'detections_topic', 'width', 'height', 'fx', 'fy', 'cx', 'cy')}
            if camera['width'] <= 0 or camera['height'] <= 0 or camera['fx'] <= 0 or camera['fy'] <= 0:
                raise ValueError(f'Calibrate image size and intrinsics for {name}')
            self.cameras[name] = camera
        self.radar_cameras = {}
        for pair in self.get_parameter('radar_camera_pairs').value:
            radar_frame, separator, camera_list = pair.partition(':')
            names = camera_list.split(',')
            if not separator or not radar_frame or radar_frame in self.radar_cameras or \
                    not names or len(names) != len(set(names)) or \
                    any(name not in self.cameras for name in names):
                raise ValueError(f'Invalid radar_camera_pairs entry: {pair}')
            self.radar_cameras[radar_frame] = set(names)
        self.tf_buffer = Buffer()
        self.tf_listener = TransformListener(self.tf_buffer, self)
        self.radar = {}
        # YOLO may finish seconds after image capture. Keep radar frames by acquisition time.
        self.radar_history = defaultdict(lambda: deque(maxlen=900))
        self.detections = {}
        self.timing_started = time.monotonic()
        self.timing_samples = {'image': [], 'periodic': []}
        self.publisher = self.create_publisher(
            FusedDetectionArray, self.get_parameter('output_topic').value, 10)
        self.radar_subs = [self.create_subscription(RadarObjectArray, topic, self.on_radar,
                           qos_profile_sensor_data) for topic in self.get_parameter('radar_topics').value]
        self.camera_subs = [self.create_subscription(Detection2DArray, camera['detections_topic'],
                            lambda msg, name=name: self.on_detections(name, msg),
                            qos_profile_sensor_data) for name, camera in self.cameras.items()]
        self.timer = self.create_timer(1.0 / hz, self.publish_snapshot)

    def on_radar(self, msg):
        self.radar[msg.header.frame_id] = msg
        self.radar_history[msg.header.frame_id].append(msg)

    def on_detections(self, name, msg):
        self.detections[name] = msg
        target_time = seconds(msg.header.stamp)
        active = []
        for history in self.radar_history.values():
            if history:
                closest = min(history, key=lambda radar: abs(seconds(radar.header.stamp) - target_time))
                if abs(seconds(closest.header.stamp) - target_time) <= self.max_age:
                    active.append(closest)
        self.publish_fused(active, {name: msg}, msg.header.stamp, msg.frame_seq,
                           trigger='image')

    def lookup(self, target, source, stamp):
        try:
            return self.tf_buffer.lookup_transform(target, source, Time.from_msg(stamp)).transform
        except TransformException as exc:
            self.get_logger().warn(f'TF unavailable {source} -> {target}: {exc}',
                                   throttle_duration_sec=5.0)
            return None

    def publish_snapshot(self):
        now = self.get_clock().now().nanoseconds * 1e-9
        active = [msg for msg in self.radar.values()
                  if 0 <= now - seconds(msg.header.stamp) <= self.stale]
        if not active:
            return
        stamp = max(active, key=lambda msg: seconds(msg.header.stamp)).header.stamp
        self.publish_fused(active, self.detections, stamp)

    def publish_fused(self, active, detections, stamp, frame_seq=0, trigger='periodic'):
        started = time.perf_counter()
        output = FusedDetectionArray()
        output.header.stamp = stamp
        output.header.frame_id = self.frame
        output.source_id = 'radar_fusion'
        output.frame_seq = frame_seq
        entries = []
        for radar in active:
            base_tf = self.lookup(self.frame, radar.header.frame_id, radar.header.stamp)
            if base_tf is None:
                continue
            for obj in radar.objects:
                point = transform_point(base_tf, (obj.x, obj.y, 0.0))
                velocity = rotate(base_tf.rotation, (obj.vx, obj.vy, 0.0))
                fused = FusedDetection()
                fused.class_id = UNKNOWN_CLASS_ID
                fused.has_3d = True
                fused.radar_source_id = radar.header.frame_id.removeprefix('radar_')
                fused.radar_sensor_id = radar.sensor_id
                fused.radar_object_id = obj.id
                fused.position_x, fused.position_y, fused.position_z = point
                fused.velocity_x, fused.velocity_y, fused.velocity_z = velocity
                fused.radar_rcs = obj.rcs
                entries.append((radar, obj, fused))
        for name, camera in self.cameras.items():
            detection_msg = detections.get(name)
            if detection_msg is None or detection_msg.image_width != camera['width'] or \
                    detection_msg.image_height != camera['height'] or \
                    abs(seconds(stamp) - seconds(detection_msg.header.stamp)) > self.stale:
                continue
            points = []
            for radar, obj, _ in entries:
                if name not in self.radar_cameras.get(radar.header.frame_id, set()) or \
                        abs(seconds(radar.header.stamp) - seconds(detection_msg.header.stamp)) > self.max_age or \
                        (self.max_match_distance and not 0 <= obj.x <= self.max_match_distance):
                    points.append((math.nan, math.nan, math.nan))
                    continue
                camera_tf = self.lookup(camera['frame'], radar.header.frame_id, radar.header.stamp)
                points.append(transform_point(camera_tf, (obj.x, obj.y, 0.0)) if camera_tf
                              else (math.nan, math.nan, math.nan))
            associations = associate(points, detection_msg.detections, camera)
            for index, box in associations.items():
                fused = entries[index][2]
                if fused.class_id != UNKNOWN_CLASS_ID and fused.score >= box.score:
                    continue
                fused.class_id = box.class_id
                fused.score = box.score
                fused.x, fused.y, fused.w, fused.h = box.x, box.y, box.w, box.h
                fused.has_track_id, fused.track_id = box.has_track_id, box.track_id
                fused.camera_source_id = detection_msg.source_id
            matched_boxes = {id(box) for box in associations.values()}
            for box in detection_msg.detections:
                if id(box) in matched_boxes:
                    continue
                camera_only = FusedDetection()
                camera_only.class_id = box.class_id
                camera_only.score = box.score
                camera_only.x, camera_only.y, camera_only.w, camera_only.h = (
                    box.x, box.y, box.w, box.h)
                camera_only.has_track_id, camera_only.track_id = box.has_track_id, box.track_id
                camera_only.camera_source_id = detection_msg.source_id
                camera_only.has_3d = False
                output.detections.append(camera_only)
        output.detections = [entry[2] for entry in entries] + list(output.detections)
        self.publisher.publish(output)
        elapsed_ms = (time.perf_counter() - started) * 1000.0
        self.timing_samples[trigger].append(elapsed_ms)
        now = time.monotonic()
        if now - self.timing_started >= 5.0:
            for kind, samples in self.timing_samples.items():
                if samples:
                    self.get_logger().info(
                        f'Fusion {kind}: {len(samples)} messages/{now - self.timing_started:.1f}s, '
                        f'avg={sum(samples) / len(samples):.1f} max={max(samples):.1f} ms')
                    samples.clear()
            self.timing_started = now


def main():
    rclpy.init()
    node = FusionNode()
    try:
        rclpy.spin(node)
    except KeyboardInterrupt:
        pass
    finally:
        node.destroy_node()
        rclpy.try_shutdown()


if __name__ == '__main__':
    main()
