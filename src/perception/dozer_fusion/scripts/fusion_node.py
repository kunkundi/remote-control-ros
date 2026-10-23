#!/usr/bin/env python3
"""Classify a combined radar snapshot using detections from three cameras."""

import math

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


def project(point, camera):
    x, y, z = point
    if z <= 0 or not all(math.isfinite(value) for value in point):
        return None
    return (camera['fx'] * x / z + camera['cx']) / camera['width'], \
           (camera['fy'] * y / z + camera['cy']) / camera['height']


def associate(points, detections, camera):
    """Greedy one-to-one association for one camera; return radar index to box."""
    pairs = []
    for ri, point in enumerate(points):
        pixel = project(point, camera)
        if pixel is None:
            continue
        u, v = pixel
        for di, box in enumerate(detections):
            if box.x <= u <= box.x + box.w and box.y <= v <= box.y + box.h:
                center_x, center_y = box.x + box.w / 2, box.y + box.h / 2
                distance = ((u - center_x) / max(box.w, 1e-6)) ** 2 + \
                           ((v - center_y) / max(box.h, 1e-6)) ** 2
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
        self.declare_parameter('output_topic', '/perception/fused_detections')
        self.declare_parameter('output_frame', 'base_link')
        self.declare_parameter('max_age_sec', 0.12)
        self.declare_parameter('stale_sec', 0.5)
        self.declare_parameter('publish_hz', 15.0)
        self.frame = self.get_parameter('output_frame').value
        self.max_age = float(self.get_parameter('max_age_sec').value)
        self.stale = float(self.get_parameter('stale_sec').value)
        hz = float(self.get_parameter('publish_hz').value)
        if min(self.max_age, self.stale, hz) <= 0:
            raise ValueError('max_age_sec, stale_sec and publish_hz must be positive')
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
        self.tf_buffer = Buffer()
        self.tf_listener = TransformListener(self.tf_buffer, self)
        self.radar = {}
        self.detections = {}
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

    def on_detections(self, name, msg):
        self.detections[name] = msg

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
        output = FusedDetectionArray()
        output.header.stamp = max(active, key=lambda msg: seconds(msg.header.stamp)).header.stamp
        output.header.frame_id = self.frame
        output.source_id = 'radar_fusion'
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
                fused.radar_sensor_id = radar.sensor_id
                fused.radar_object_id = obj.id
                fused.position_x, fused.position_y, fused.position_z = point
                fused.velocity_x, fused.velocity_y, fused.velocity_z = velocity
                fused.radar_rcs = obj.rcs
                entries.append((radar, obj, fused))
        for name, camera in self.cameras.items():
            detection_msg = self.detections.get(name)
            if detection_msg is None or detection_msg.image_width != camera['width'] or \
                    detection_msg.image_height != camera['height']:
                continue
            points = []
            for radar, obj, _ in entries:
                if abs(seconds(radar.header.stamp) - seconds(detection_msg.header.stamp)) > self.max_age:
                    points.append((math.nan, math.nan, math.nan))
                    continue
                camera_tf = self.lookup(camera['frame'], radar.header.frame_id, radar.header.stamp)
                points.append(transform_point(camera_tf, (obj.x, obj.y, 0.0)) if camera_tf
                              else (math.nan, math.nan, math.nan))
            for index, box in associate(points, detection_msg.detections, camera).items():
                fused = entries[index][2]
                if fused.class_id != UNKNOWN_CLASS_ID and fused.score >= box.score:
                    continue
                fused.class_id = box.class_id
                fused.score = box.score
                fused.x, fused.y, fused.w, fused.h = box.x, box.y, box.w, box.h
                fused.has_track_id, fused.track_id = box.has_track_id, box.track_id
                fused.camera_source_id = detection_msg.source_id
        output.detections = [entry[2] for entry in entries]
        self.publisher.publish(output)


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
