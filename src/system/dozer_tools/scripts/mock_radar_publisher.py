#!/usr/bin/env python3
"""模拟多台雷达发布 RadarObjectArray，供下游模块在雷达未接入时开发联调。"""

import math

import rclpy
from rclpy.node import Node
from rclpy.qos import qos_profile_sensor_data

from dozer_msgs.msg import RadarObject, RadarObjectArray


class MockRadarPublisher(Node):

    def __init__(self):
        super().__init__('mock_radar_publisher')
        self.declare_parameter('radars', ['front_left', 'front_right', 'rear'])
        self.declare_parameter('rate_hz', 30.0)
        self.declare_parameter('objects_per_radar', 10)

        names = self.get_parameter('radars').value
        rate = float(self.get_parameter('rate_hz').value)
        self._num_objects = int(self.get_parameter('objects_per_radar').value)
        self._radars = []
        for sensor_id, name in enumerate(names):
            pub = self.create_publisher(
                RadarObjectArray, f'/radar/{name}/objects', qos_profile_sensor_data)
            self._radars.append((sensor_id, name, pub))
        self._meas_count = 0
        self.create_timer(1.0 / rate, self._on_timer)
        self.get_logger().info(f'模拟雷达: {names}, {rate} Hz')

    def _on_timer(self):
        stamp = self.get_clock().now().to_msg()
        t = self._meas_count / 30.0
        for sensor_id, name, pub in self._radars:
            msg = RadarObjectArray()
            msg.header.stamp = stamp
            msg.header.frame_id = f'radar_{name}'
            msg.sensor_id = sensor_id
            msg.meas_count = self._meas_count & 0xFFFF
            for i in range(self._num_objects):
                obj = RadarObject()
                obj.id = i
                obj.x = 5.0 + 3.0 * i + math.sin(t + i)
                obj.y = 2.0 * math.cos(0.5 * t + i)
                obj.vx = math.cos(t + i)
                obj.vy = -math.sin(0.5 * t + i)
                obj.rcs = 10.0
                msg.objects.append(obj)
            msg.nof_objects_reported = len(msg.objects)
            pub.publish(msg)
        self._meas_count += 1


def main():
    rclpy.init()
    node = MockRadarPublisher()
    try:
        rclpy.spin(node)
    except KeyboardInterrupt:
        pass
    finally:
        node.destroy_node()
        rclpy.try_shutdown()


if __name__ == '__main__':
    main()
