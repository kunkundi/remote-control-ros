#!/usr/bin/env python3
"""Run one TensorRT YOLO engine on frames from up to six shared cameras."""

import argparse
import resource
import time
from pathlib import Path

from ament_index_python.packages import get_package_share_path
import rclpy
from rclpy.node import Node
from std_msgs.msg import Header
from dozer_msgs.msg import Detection2D, Detection2DArray
from camera_shm import CAMERA_NAMES, CameraShmReader
from batch_predictor import BatchedDetectionPredictor


class YoloBatchNode(Node):
    def __init__(self, camera_names):
        super().__init__('yolo_batch')
        model = get_package_share_path('dozer_perception') / 'model' / 'best_int8_batch6.engine'
        self.declare_parameter('model_path', str(model))
        self.declare_parameter('confidence', 0.25)
        self.declare_parameter('device', 0)
        self.declare_parameter('shm_prefix', 'dozer_camera_')
        self.declare_parameter('max_frame_age_sec', 0.2)
        self.declare_parameter('batch_wait_sec', 0.04)
        self.names = camera_names
        self.batch_size = 6
        path = Path(self.get_parameter('model_path').value)
        if not path.is_file() or path.suffix != '.engine':
            raise ValueError(f'Batch TensorRT engine missing: {path}')
        from ultralytics import YOLO
        import torch
        self.device = int(self.get_parameter('device').value)
        if not torch.cuda.is_available():
            raise RuntimeError('CUDA is unavailable')
        self.model = YOLO(str(path))
        self.model_path = str(path)
        self.confidence = float(self.get_parameter('confidence').value)
        self.wait_sec = float(self.get_parameter('batch_wait_sec').value)
        if not 0 <= self.wait_sec <= 0.2:
            raise ValueError('batch_wait_sec must be in [0, 0.2]')
        self.readers = {name: CameraShmReader(
            self.get_parameter('shm_prefix').value + name,
            self.get_parameter('max_frame_age_sec').value) for name in self.names}
        self.detection_publishers = {name: self.create_publisher(
            Detection2DArray, f'/perception/{name}/detections', 10) for name in self.names}
        self.pending = {}
        self.pending_since = None
        self.timing_started = time.monotonic()
        self.timings = []
        self.stage_timings = []
        self.frame_count = 0
        self.timer = self.create_timer(1.0 / 60.0, self.on_timer)

    def on_timer(self):
        for name, reader in self.readers.items():
            try:
                frame = reader.read()
            except (OSError, ValueError) as exc:
                self.get_logger().warn(f'{name}: {exc}', throttle_duration_sec=5.0)
                continue
            if frame is None:
                continue
            if frame.frame_id != name:
                self.get_logger().error(f'Shared camera source ID mismatch: {name}',
                                        throttle_duration_sec=5.0)
                continue
            self.pending[name] = (frame, *reader.last_timing_ms)
            if self.pending_since is None:
                self.pending_since = time.monotonic()
        if not self.pending:
            return
        if len(self.pending) < len(self.names) and \
                time.monotonic() - self.pending_since < self.wait_sec:
            return
        selected = [(name, *self.pending.pop(name)) for name in self.names if name in self.pending]
        self.pending_since = None
        images = [frame.image for _, frame, _, _ in selected]
        # The engine has a fixed batch of six. Padding is ignored on output.
        images.extend([images[-1]] * (self.batch_size - len(images)))
        started = time.perf_counter()
        results = self.model.predict(images, conf=self.confidence,
                                     device=self.device, verbose=False,
                                     predictor=BatchedDetectionPredictor)
        elapsed_ms = (time.perf_counter() - started) * 1000.0
        if len(results) != self.batch_size:
            raise RuntimeError(f'Expected {self.batch_size} TensorRT results, got {len(results)}')
        publish_started = time.perf_counter()
        for (name, frame, _, _), result in zip(selected, results):
            self.publish_detection(name, frame, result)
        publish_ms = (time.perf_counter() - publish_started) * 1000.0
        # Ultralytics reports these stages per image; multiply by the fixed engine batch.
        speed = results[0].speed
        stages = {key: speed[key] * self.batch_size for key in
                  ('preprocess', 'inference', 'postprocess')}
        stages['read'] = sum(item[2] for item in selected)
        stages['convert'] = sum(item[3] for item in selected)
        stages['format_publish'] = publish_ms
        stages['predict_other'] = max(0.0, elapsed_ms - sum(stages[key] for key in
                                      ('preprocess', 'inference', 'postprocess')))
        self.record_timing(selected, elapsed_ms + publish_ms, stages)

    def record_timing(self, selected, elapsed_ms, stages):
        self.stage_timings.append(stages)
        self.timings.append(elapsed_ms)
        self.frame_count += len(selected)
        now = time.monotonic()
        if now - self.timing_started >= 5.0:
            count = len(self.timings)
            self.get_logger().info(
                f'Batch YOLO: {count} batches, {self.frame_count} camera frames/'
                f'{now - self.timing_started:.1f}s, '
                f'total avg={sum(self.timings) / count:.1f} '
                f'max={max(self.timings):.1f} ms/batch, '
                f'peak RSS={resource.getrusage(resource.RUSAGE_SELF).ru_maxrss / 1024:.0f} MiB')
            averages = {key: sum(sample[key] for sample in self.stage_timings) / count
                        for key in self.stage_timings[0]}
            self.get_logger().info(
                'Batch stages avg ms/batch: '
                + ', '.join(f'{key}={value:.1f}' for key, value in averages.items())
                + '; read/convert count only frames used, format_publish includes box conversion')
            self.timings.clear()
            self.stage_timings.clear()
            self.frame_count = 0
            self.timing_started = now

    def publish_detection(self, name, frame, result):
        height, width = frame.image.shape[:2]
        output = Detection2DArray()
        output.header = Header()
        output.header.stamp.sec, output.header.stamp.nanosec = divmod(
            frame.timestamp_ns, 1_000_000_000)
        output.header.frame_id = name
        output.source_id = name
        output.frame_seq = frame.sequence
        output.image_width, output.image_height = width, height
        output.model_name = self.model_path
        # Transfer all boxes for this image once; scalar reads from CUDA synchronize per box.
        boxes = result.boxes.data.cpu().numpy()
        for row in boxes:
            x1, y1, x2, y2 = row[:4]
            detection = Detection2D()
            detection.class_id = int(row[-1])
            detection.score = float(row[-2])
            detection.x = max(0.0, min(1.0, x1 / width))
            detection.y = max(0.0, min(1.0, y1 / height))
            detection.w = max(0.0, min(1.0 - detection.x, (x2 - x1) / width))
            detection.h = max(0.0, min(1.0 - detection.y, (y2 - y1) / height))
            output.detections.append(detection)
        self.detection_publishers[name].publish(output)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--camera-names', required=True)
    args, ros_args = parser.parse_known_args()
    names = tuple(args.camera_names.split(','))
    if not names or len(names) > 6 or len(names) != len(set(names)) or any(
            name not in CAMERA_NAMES for name in names):
        parser.error('camera-names must be unique names from ' + ','.join(CAMERA_NAMES))
    rclpy.init(args=ros_args)
    node = YoloBatchNode(names)
    try:
        rclpy.spin(node)
    except KeyboardInterrupt:
        pass
    finally:
        node.destroy_node()
        rclpy.try_shutdown()


if __name__ == '__main__':
    main()
