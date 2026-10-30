import fcntl
import os
from pathlib import Path
import subprocess
import sys
import time
from types import SimpleNamespace
import unittest
import uuid

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'scripts'))
from camera_shm import CameraShmReader, HEADER


class CameraShmTests(unittest.TestCase):
    def setUp(self):
        self.name = 'dozer_test_' + uuid.uuid4().hex
        self.path = Path('/dev/shm') / self.name
        self.reader = CameraShmReader(self.name)

    def tearDown(self):
        self.path.unlink(missing_ok=True)
        Path(str(self.path) + '.owner').unlink(missing_ok=True)

    def write_frame(self, sequence=1, published=None, version=1, width=4):
        payload = bytes([81] * 16 + [90] * 4 + [240] * 4)
        header = HEADER.pack(b'DZRI420\0', version, 88, sequence, 123456789,
                             time.monotonic_ns() if published is None else published,
                             width, 4, len(payload), 0, b'front_left')
        self.path.write_bytes(header + payload)

    def test_snapshot_and_duplicate(self):
        self.write_frame()
        frame = self.reader.read()
        self.assertEqual(frame.timestamp_ns, 123456789)
        self.assertEqual(frame.frame_id, 'front_left')
        self.assertEqual(frame.image.shape, (4, 4, 3))
        self.assertGreater(int(frame.image[0, 0, 2]), 240)
        self.assertLess(int(frame.image[0, 0, 0]), 5)
        self.assertIsNone(self.reader.read())

    def test_missing_stale_and_locked(self):
        self.assertIsNone(self.reader.read())
        self.write_frame(published=time.monotonic_ns() - 1_000_000_000)
        self.assertIsNone(self.reader.read())
        self.write_frame()
        with self.path.open('rb') as producer:
            fcntl.flock(producer, fcntl.LOCK_EX)
            self.assertIsNone(self.reader.read())
        self.assertIsNotNone(self.reader.read())

    def test_replacement_producer(self):
        self.write_frame()
        self.assertIsNotNone(self.reader.read())
        replacement = self.path.with_suffix('.old')
        self.path.rename(replacement)
        try:
            self.write_frame()
            self.assertIsNotNone(self.reader.read())
        finally:
            replacement.unlink()

    def test_invalid_frames(self):
        self.path.write_bytes(b'bad')
        with self.assertRaises(ValueError):
            self.reader.read()
        self.write_frame(version=2)
        with self.assertRaises(ValueError):
            self.reader.read()
        self.write_frame(width=3)
        with self.assertRaises(ValueError):
            self.reader.read()
        self.write_frame()
        self.path.write_bytes(self.path.read_bytes()[:-1])
        with self.assertRaises(ValueError):
            self.reader.read()

    def test_yolo_metadata_and_viewer_matching(self):
        import rclpy
        from detection_viewer import DetectionViewer
        from yolo_node import YoloNode

        self.write_frame(sequence=42)
        frame = self.reader.read()
        outputs = []
        errors = []
        parameters = {'source_id': 'front_left', 'confidence': 0.25, 'model_path': 'test.pt'}
        node = SimpleNamespace(
            frame_reader=SimpleNamespace(read=lambda: frame),
            get_parameter=lambda name: SimpleNamespace(value=parameters[name]),
            get_logger=lambda: SimpleNamespace(error=lambda *args, **kwargs: errors.append(args)),
            device=0,
            model=SimpleNamespace(predict=lambda *args, **kwargs: [SimpleNamespace(boxes=[])]),
            publisher=SimpleNamespace(publish=outputs.append))
        node.infer = lambda image, header, sequence: YoloNode.infer(node, image, header, sequence)
        YoloNode.on_shared_frame(node)
        self.assertEqual(outputs[0].frame_seq, 42)
        self.assertEqual(outputs[0].header.stamp.nanosec, 123456789)
        self.assertEqual(outputs[0].header.frame_id, 'front_left')
        self.assertEqual(outputs[0].image_width, 4)
        parameters['source_id'] = 'front_right'
        YoloNode.on_shared_frame(node)
        self.assertEqual(len(outputs), 1)
        self.assertEqual(len(errors), 1)

        rclpy.init(args=['--ros-args', '-p', 'show_window:=false'])
        viewer = DetectionViewer()
        try:
            self.assertFalse(viewer.image_publishers)
            subscriptions = viewer.get_subscriber_names_and_types_by_node(
                viewer.get_name(), viewer.get_namespace())
            self.assertFalse(any('sensor_msgs/msg/Image' in types for _, types in subscriptions))
            viewer.frame_readers = {'front_left': CameraShmReader(self.name)}
            viewer.on_detections('front_left', outputs[0])
            self.assertNotIn('front_left', viewer.rendered)
            self.write_frame(sequence=42)
            viewer.read_shared_frames()
            self.assertIn('front_left', viewer.rendered)
            viewer.rendered.clear()
            outputs[0].header.stamp.nanosec += 1
            viewer.on_detections('front_left', outputs[0])
            self.assertNotIn('front_left', viewer.rendered)
        finally:
            viewer.destroy_node()
            rclpy.shutdown()

    @unittest.skipUnless(os.environ.get('DOZER_SHM_TEST_WRITER'), 'C++ fixture not selected')
    def test_cpp_producer(self):
        process = subprocess.Popen([os.environ['DOZER_SHM_TEST_WRITER'], '--fixture', self.name],
                                   stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True)
        try:
            self.assertEqual(process.stdout.readline().strip(), 'ready')
            frame = self.reader.read()
            self.assertIsNotNone(frame)
            self.assertEqual(frame.timestamp_ns, 123456)
            self.assertEqual(frame.frame_id, 'front_left')
            self.assertEqual(frame.sequence, 1)
        finally:
            process.communicate('\n', timeout=5)
        self.assertEqual(process.returncode, 0)
        self.assertIsNone(self.reader.read())


if __name__ == '__main__':
    unittest.main()