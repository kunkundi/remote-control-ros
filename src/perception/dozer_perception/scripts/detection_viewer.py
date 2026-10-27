#!/usr/bin/env python3
"""Draw YOLO boxes and radar fusion details on the corresponding camera frame."""

import os
import math
import time
from collections import OrderedDict, deque

import cv2
import numpy as np
import rclpy
from rclpy.node import Node
from rclpy.qos import qos_profile_sensor_data
from sensor_msgs.msg import Image
from dozer_msgs.msg import Detection2DArray, FusedDetectionArray, RadarObjectArray


UNKNOWN_CLASS_ID = 4294967295
COCO_NAMES = [
    'person', 'bicycle', 'car', 'motorcycle', 'airplane', 'bus', 'train', 'truck',
    'boat', 'traffic light', 'fire hydrant', 'stop sign', 'parking meter', 'bench',
    'bird', 'cat', 'dog', 'horse', 'sheep', 'cow', 'elephant', 'bear', 'zebra',
    'giraffe', 'backpack', 'umbrella', 'handbag', 'tie', 'suitcase', 'frisbee',
    'skis', 'snowboard', 'sports ball', 'kite', 'baseball bat', 'baseball glove',
    'skateboard', 'surfboard', 'tennis racket', 'bottle', 'wine glass', 'cup',
    'fork', 'knife', 'spoon', 'bowl', 'banana', 'apple', 'sandwich', 'orange',
    'broccoli', 'carrot', 'hot dog', 'pizza', 'donut', 'cake', 'chair', 'couch',
    'potted plant', 'bed', 'dining table', 'toilet', 'tv', 'laptop', 'mouse',
    'remote', 'keyboard', 'cell phone', 'microwave', 'oven', 'toaster', 'sink',
    'refrigerator', 'book', 'clock', 'vase', 'scissors', 'teddy bear', 'hair drier',
    'toothbrush',
]


def stamp_ns(stamp):
    return stamp.sec * 1_000_000_000 + stamp.nanosec


def to_bgr(msg):
    channels = {'bgr8': 3, 'rgb8': 3, 'bgra8': 4, 'rgba8': 4}
    if msg.encoding not in channels:
        raise ValueError(f'Unsupported image encoding: {msg.encoding}')
    width_bytes = msg.width * channels[msg.encoding]
    if msg.step < width_bytes or len(msg.data) < msg.step * msg.height:
        raise ValueError('Invalid image buffer')
    rows = np.frombuffer(msg.data, dtype=np.uint8).reshape(msg.height, msg.step)
    pixels = rows[:, :width_bytes].reshape(msg.height, msg.width, channels[msg.encoding])
    if msg.encoding == 'rgb8':
        return cv2.cvtColor(pixels, cv2.COLOR_RGB2BGR)
    if msg.encoding == 'rgba8':
        return cv2.cvtColor(pixels, cv2.COLOR_RGBA2BGR)
    if msg.encoding == 'bgra8':
        return cv2.cvtColor(pixels, cv2.COLOR_BGRA2BGR)
    return pixels.copy()


def matching_fused(box, source, fused):
    if fused is None:
        return None
    for obj in fused.detections:
        if obj.class_id == UNKNOWN_CLASS_ID or obj.camera_source_id != source:
            continue
        if obj.class_id == box.class_id and all(abs(a - b) < 1e-4 for a, b in
                zip((obj.x, obj.y, obj.w, obj.h), (box.x, box.y, box.w, box.h))):
            return obj
    return None


def draw_radar(msg, forward_m=6.0, lateral_m=3.0):
    """Render radar x forward and y left in its own coordinate frame."""
    width, height = 900, 700
    image = np.full((height, width, 3), (28, 28, 28), dtype=np.uint8)
    origin_x, origin_y = width // 2, height - 55
    scale = min((width - 100) / (2 * lateral_m), (height - 130) / forward_m)

    for distance in range(0, int(forward_m) + 1):
        py = round(origin_y - distance * scale)
        cv2.line(image, (50, py), (width - 50, py), (65, 65, 65), 1)
        cv2.putText(image, f'{distance}m', (8, py + 4),
                    cv2.FONT_HERSHEY_SIMPLEX, 0.45, (170, 170, 170), 1)
    for lateral in range(-int(lateral_m), int(lateral_m) + 1):
        px = round(origin_x - lateral * scale)
        cv2.line(image, (px, 55), (px, origin_y), (65, 65, 65), 1)
        if lateral:
            cv2.putText(image, f'{abs(lateral)}m {"L" if lateral > 0 else "R"}',
                        (px - 23, height - 18), cv2.FONT_HERSHEY_SIMPLEX,
                        0.42, (170, 170, 170), 1)
    cv2.circle(image, (origin_x, origin_y), 7, (255, 255, 255), -1)
    cv2.putText(image, 'RADAR', (origin_x + 10, origin_y - 8),
                cv2.FONT_HERSHEY_SIMPLEX, 0.45, (230, 230, 230), 1)

    visible = 0
    if msg is not None:
        for obj in msg.objects:
            if not (math.isfinite(obj.x) and math.isfinite(obj.y) and
                    0 <= obj.x <= forward_m and abs(obj.y) <= lateral_m):
                continue
            px = round(origin_x - obj.y * scale)
            py = round(origin_y - obj.x * scale)
            moving = math.hypot(obj.vx, obj.vy) > 0.3
            color = (0, 180, 255) if moving else (0, 230, 90)
            cv2.circle(image, (px, py), 5, color, -1)
            cv2.putText(image, str(obj.id), (px + 7, py - 6),
                        cv2.FONT_HERSHEY_SIMPLEX, 0.38, color, 1)
            visible += 1

    count = len(msg.objects) if msg is not None else 0
    reported = msg.nof_objects_reported if msg is not None else 0
    cv2.putText(image, f'front_left radar  received: {count} / reported: {reported}  in 0-6m view: {visible}',
                (12, 28), cv2.FONT_HERSHEY_SIMPLEX, 0.57, (240, 240, 240), 2)
    cv2.putText(image, 'x: forward   y: left   green: near-zero relative speed   orange: moving',
                (12, 49), cv2.FONT_HERSHEY_SIMPLEX, 0.46, (190, 190, 190), 1)
    return image


class DetectionViewer(Node):
    def __init__(self):
        super().__init__('detection_viewer')
        self.declare_parameter('show_window', True)
        self.declare_parameter('window_name', 'Dozer detections')
        self.declare_parameter('radar_window_name', 'Dozer radar front_left')
        self.show_window = bool(self.get_parameter('show_window').value)
        if self.show_window and not (os.environ.get('DISPLAY') or os.environ.get('WAYLAND_DISPLAY')):
            self.get_logger().warn('No graphical display; publishing annotated images without a window')
            self.show_window = False
        self.frames = {'front_left': deque(maxlen=50), 'front_right': deque(maxlen=50)}
        self.rendered = {}
        self.fused = None
        self.fused_by_stamp = OrderedDict()
        self.latest_detections = {}
        self.radar = None
        self.radar_received_at = None
        self.input_subscriptions = []
        self.image_publishers = {}
        for name, detections_topic in (
                ('front_left', '/perception/front_left/detections'),
                ('front_right', '/perception/front_right/detections')):
            self.input_subscriptions.append(self.create_subscription(
                Image, f'/camera/{name}/image_small',
                lambda msg, key=name: self.on_image(key, msg), qos_profile_sensor_data))
            self.input_subscriptions.append(self.create_subscription(
                Detection2DArray, detections_topic,
                lambda msg, key=name: self.on_detections(key, msg), 10))
            self.image_publishers[name] = self.create_publisher(
                Image, f'/perception/{name}/annotated_image', qos_profile_sensor_data)
        self.input_subscriptions.append(self.create_subscription(
            FusedDetectionArray, '/perception/fused_detections', self.on_fused, 10))
        self.input_subscriptions.append(self.create_subscription(
            RadarObjectArray, '/radar/front_left/objects', self.on_radar, qos_profile_sensor_data))
        if self.show_window:
            cv2.namedWindow(self.get_parameter('window_name').value, cv2.WINDOW_NORMAL)
            cv2.resizeWindow(self.get_parameter('window_name').value, 1280, 360)
            cv2.namedWindow(self.get_parameter('radar_window_name').value, cv2.WINDOW_NORMAL)
            cv2.resizeWindow(self.get_parameter('radar_window_name').value, 900, 700)
            self.timer = self.create_timer(0.1, self.show)

    def on_image(self, name, msg):
        self.frames[name].append(msg)

    def on_fused(self, msg):
        self.fused = msg
        key = stamp_ns(msg.header.stamp)
        self.fused_by_stamp[key] = msg
        while len(self.fused_by_stamp) > 300:
            self.fused_by_stamp.popitem(last=False)
        for name, detection in self.latest_detections.items():
            if stamp_ns(detection.header.stamp) == key:
                self.on_detections(name, detection)

    def on_radar(self, msg):
        self.radar = msg
        self.radar_received_at = time.monotonic()

    def on_detections(self, name, msg):
        self.latest_detections[name] = msg
        frame = next((image for image in reversed(self.frames[name])
                      if stamp_ns(image.header.stamp) == stamp_ns(msg.header.stamp)), None)
        if frame is None:
            self.get_logger().warn(f'No matching image for {name} detection stamp',
                                   throttle_duration_sec=5.0)
            return
        try:
            image = to_bgr(frame)
        except ValueError as exc:
            self.get_logger().warn(str(exc), throttle_duration_sec=5.0)
            return
        fused = self.fused_by_stamp.get(stamp_ns(msg.header.stamp))
        height, width = image.shape[:2]
        unknown_count = sum(d.class_id == UNKNOWN_CLASS_ID for d in fused.detections) if fused else 0
        cv2.putText(image, f'{name}  YOLO: {len(msg.detections)}  radar unknown: {unknown_count}',
                    (8, 22), cv2.FONT_HERSHEY_SIMPLEX, 0.55, (255, 255, 255), 2)
        for box in msg.detections:
            x1 = max(0, min(width - 1, int(box.x * width)))
            y1 = max(0, min(height - 1, int(box.y * height)))
            x2 = max(0, min(width - 1, int((box.x + box.w) * width)))
            y2 = max(0, min(height - 1, int((box.y + box.h) * height)))
            match = matching_fused(box, msg.source_id, fused)
            color = (0, 255, 0) if match else (0, 200, 255)
            cv2.rectangle(image, (x1, y1), (x2, y2), color, 2)
            label = COCO_NAMES[box.class_id] if box.class_id < len(COCO_NAMES) else str(box.class_id)
            label += f' {box.score:.2f}'
            if match:
                label += f'  3D ({match.position_x:.1f},{match.position_y:.1f},{match.position_z:.1f})m'
            cv2.putText(image, label, (x1, max(40, y1 - 7)),
                        cv2.FONT_HERSHEY_SIMPLEX, 0.45, color, 2)
        output = Image()
        output.header = frame.header
        output.height, output.width = height, width
        output.encoding = 'bgr8'
        output.is_bigendian = 0
        output.step = width * 3
        output.data = image.tobytes()
        self.image_publishers[name].publish(output)
        self.rendered[name] = image

    def show(self):
        if self.rendered:
            panels = [self.rendered.get(name, np.zeros((360, 640, 3), np.uint8))
                      for name in ('front_left', 'front_right')]
            cv2.imshow(self.get_parameter('window_name').value, np.hstack(panels))
        fresh = self.radar_received_at is not None and time.monotonic() - self.radar_received_at < 1.0
        radar_image = draw_radar(self.radar if fresh else None)
        if not fresh:
            cv2.putText(radar_image, 'WAITING FOR RADAR DATA', (240, 350),
                        cv2.FONT_HERSHEY_SIMPLEX, 0.8, (0, 200, 255), 2)
        cv2.imshow(self.get_parameter('radar_window_name').value, radar_image)
        cv2.waitKey(1)


def main():
    rclpy.init()
    node = DetectionViewer()
    try:
        rclpy.spin(node)
    except KeyboardInterrupt:
        pass
    finally:
        if node.show_window:
            cv2.destroyAllWindows()
        node.destroy_node()
        rclpy.try_shutdown()


if __name__ == '__main__':
    main()
