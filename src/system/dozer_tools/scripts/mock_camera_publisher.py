#!/usr/bin/env python3
"""Publish synthetic images for exercising the three-camera ROS topic chain."""

import rclpy
from rclpy.node import Node
from rclpy.qos import qos_profile_sensor_data
from sensor_msgs.msg import Image


class MockCameraPublisher(Node):
    def __init__(self):
        super().__init__('mock_camera_publisher')
        self.declare_parameter('cameras', ['front_left', 'front_right', 'rear'])
        self.declare_parameter('width', 640)
        self.declare_parameter('height', 360)
        self.declare_parameter('rate_hz', 1.0)
        width = int(self.get_parameter('width').value)
        height = int(self.get_parameter('height').value)
        rate = float(self.get_parameter('rate_hz').value)
        if width <= 0 or height <= 0 or rate <= 0:
            raise ValueError('width, height and rate_hz must be positive')
        self.width, self.height = width, height
        self.pixels = bytes([96, 96, 96]) * (width * height)
        names = self.get_parameter('cameras').value
        self._camera_pubs = [(name, self.create_publisher(
            Image, f'/camera/{name}/image_small', qos_profile_sensor_data)) for name in names]
        self.timer = self.create_timer(1.0 / rate, self.publish_images)
        self.get_logger().info(f'Synthetic cameras: {names}, {width}x{height}, {rate} Hz')

    def publish_images(self):
        stamp = self.get_clock().now().to_msg()
        for name, publisher in self._camera_pubs:
            image = Image()
            image.header.stamp = stamp
            image.header.frame_id = f'camera_{name}'
            image.height, image.width = self.height, self.width
            image.encoding = 'bgr8'
            image.is_bigendian = 0
            image.step = self.width * 3
            image.data = self.pixels
            publisher.publish(image)


def main():
    rclpy.init()
    node = MockCameraPublisher()
    try:
        rclpy.spin(node)
    finally:
        node.destroy_node()
        rclpy.try_shutdown()


if __name__ == '__main__':
    main()
