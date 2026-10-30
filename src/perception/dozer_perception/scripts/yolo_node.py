#!/usr/bin/env python3
"""Run YOLO on shared camera frames and publish normalized ROS detections."""

from pathlib import Path

import numpy as np
import rclpy
from rclpy.node import Node
from rclpy.qos import HistoryPolicy, QoSProfile, ReliabilityPolicy
from sensor_msgs.msg import Image
from std_msgs.msg import Header
from dozer_msgs.msg import Detection2D, Detection2DArray
from camera_shm import CameraShmReader


class YoloNode(Node):
    def __init__(self):
        super().__init__('yolo_node')
        self.declare_parameter('image_topic', '/camera/front_left/image_small')
        self.declare_parameter('image_backend', 'shm')
        self.declare_parameter('shm_name', 'dozer_camera_front_left')
        self.declare_parameter('max_frame_age_sec', 0.2)
        self.declare_parameter('detections_topic', '/perception/front_left/detections')
        self.declare_parameter('source_id', 'front_left')
        self.declare_parameter('model_path', '')
        self.declare_parameter('confidence', 0.25)
        self.declare_parameter('device', 0)
        path = self.get_parameter('model_path').value
        if not path:
            raise ValueError('model_path must point to a YOLO model or TensorRT engine')
        if not Path(path).is_file():
            raise FileNotFoundError(f'YOLO model not found: {path}')
        from ultralytics import YOLO
        import torch
        self.device = int(self.get_parameter('device').value)
        if not torch.cuda.is_available():
            raise RuntimeError('GPU inference requested, but PyTorch cannot access CUDA')
        # The available cuDNN build has no convolution engine for this Orin; native CUDA works.
        torch.backends.cudnn.enabled = False
        self.model = YOLO(path)
        self.get_logger().info(f'YOLO inference device: {self.device}')
        self.seq = 0
        self.publisher = self.create_publisher(
            Detection2DArray, self.get_parameter('detections_topic').value, 10)
        backend = self.get_parameter('image_backend').value
        if backend == 'shm':
            self.frame_reader = CameraShmReader(
                self.get_parameter('shm_name').value,
                self.get_parameter('max_frame_age_sec').value)
            self.frame_timer = self.create_timer(1.0 / 60.0, self.on_shared_frame)
        elif backend == 'ros':
            self.subscription = self.create_subscription(
                Image, self.get_parameter('image_topic').value, self.on_image,
                QoSProfile(history=HistoryPolicy.KEEP_LAST, depth=1,
                           reliability=ReliabilityPolicy.BEST_EFFORT))
        else:
            raise ValueError('image_backend must be shm or ros')

    def on_shared_frame(self):
        try:
            frame = self.frame_reader.read()
        except (OSError, ValueError) as exc:
            self.get_logger().error(str(exc), throttle_duration_sec=5.0)
            return
        if frame is None:
            return
        if frame.frame_id != self.get_parameter('source_id').value:
            self.get_logger().error('Shared camera source ID mismatch', throttle_duration_sec=5.0)
            return
        header = Header()
        header.stamp.sec, header.stamp.nanosec = divmod(frame.timestamp_ns, 1_000_000_000)
        header.frame_id = frame.frame_id
        self.infer(frame.image, header, frame.sequence)

    def on_image(self, msg):
        encodings = {'rgb8': (3, True), 'bgr8': (3, False),
                     'rgba8': (4, True), 'bgra8': (4, False)}
        if msg.encoding not in encodings:
            self.get_logger().warn(f'Unsupported image encoding: {msg.encoding}',
                                   throttle_duration_sec=5.0)
            return
        channels, rgb = encodings[msg.encoding]
        row_bytes = msg.width * channels
        if msg.step < row_bytes or len(msg.data) < msg.step * msg.height:
            self.get_logger().error('Invalid image buffer size')
            return
        pixels = np.frombuffer(msg.data, dtype=np.uint8).reshape(msg.height, msg.step)
        image = pixels[:, :row_bytes].reshape(msg.height, msg.width, channels)
        if channels == 4:
            image = image[:, :, :3]
        if rgb:
            image = image[:, :, ::-1]
        image = np.ascontiguousarray(image)
        self.infer(image, msg.header, self.seq)
        self.seq += 1

    def infer(self, image, header, sequence):
        height, width = image.shape[:2]
        result = self.model.predict(image, conf=self.get_parameter('confidence').value,
                                    device=self.device, verbose=False)[0]
        output = Detection2DArray()
        output.header = header
        output.source_id = self.get_parameter('source_id').value
        output.frame_seq = sequence
        output.image_width, output.image_height = width, height
        output.model_name = self.get_parameter('model_path').value
        for box in result.boxes:
            x1, y1, x2, y2 = box.xyxy[0].tolist()
            detection = Detection2D()
            detection.class_id = int(box.cls[0])
            detection.score = float(box.conf[0])
            detection.x = max(0.0, min(1.0, x1 / width))
            detection.y = max(0.0, min(1.0, y1 / height))
            detection.w = max(0.0, min(1.0 - detection.x, (x2 - x1) / width))
            detection.h = max(0.0, min(1.0 - detection.y, (y2 - y1) / height))
            output.detections.append(detection)
        self.publisher.publish(output)


def main():
    rclpy.init()
    node = YoloNode()
    try:
        rclpy.spin(node)
    except KeyboardInterrupt:
        pass
    finally:
        node.destroy_node()
        rclpy.try_shutdown()


if __name__ == '__main__':
    main()
