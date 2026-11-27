#!/usr/bin/python3
"""按订阅需求将 edge 的共享内存图像发布到 ROS。"""

import rclpy
from rclpy.node import Node
from rclpy.qos import qos_profile_sensor_data
from sensor_msgs.msg import Image

from camera_shm import CAMERA_NAMES, CameraShmReader


class CameraImagePublisher(Node):
    def __init__(self):
        super().__init__('camera_image_publisher')
        prefix = self.declare_parameter('shm_prefix', 'dozer_camera_').value
        self.streams = []
        for name in CAMERA_NAMES:
            publisher = self.create_publisher(Image, f'/camera/{name}/image_small',
                                              qos_profile_sensor_data)
            self.streams.append((name, publisher, CameraShmReader(prefix + name)))
        self.create_timer(1.0 / 60.0, self.publish_images)

    def publish_images(self):
        for name, publisher, reader in self.streams:
            if publisher.get_subscription_count() == 0:
                continue
            try:
                frame = reader.read()
                if frame is None:
                    continue
                if frame.frame_id != name:
                    raise ValueError(f'共享帧来自 {frame.frame_id}')
            except (OSError, ValueError) as exc:
                self.get_logger().warning(f'跳过相机 {name} 的帧：{exc}', throttle_duration_sec=5.0)
                continue

            msg = Image()
            msg.header.stamp.sec, msg.header.stamp.nanosec = divmod(frame.timestamp_ns, 1_000_000_000)
            msg.header.frame_id = f'camera_{name}'
            msg.height, msg.width = frame.image.shape[:2]
            msg.encoding = 'bgr8'
            msg.step = msg.width * 3
            msg.data.frombytes(frame.image)
            publisher.publish(msg)


def main():
    rclpy.init()
    node = CameraImagePublisher()
    try:
        rclpy.spin(node)
    except KeyboardInterrupt:
        pass
    finally:
        node.destroy_node()
        rclpy.try_shutdown()


if __name__ == '__main__':
    main()
