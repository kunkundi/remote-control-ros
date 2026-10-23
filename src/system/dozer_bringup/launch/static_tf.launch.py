"""根据 config/extrinsics.yaml 发布传感器静态外参。"""

import os

import yaml
from ament_index_python.packages import get_package_share_directory
from launch import LaunchDescription
from launch_ros.actions import Node


def generate_launch_description():
    path = os.path.join(
        get_package_share_directory('dozer_bringup'), 'config', 'extrinsics.yaml')
    with open(path, 'r', encoding='utf-8') as f:
        sensors = yaml.safe_load(f).get('sensors', [])

    nodes = []
    for s in sensors:
        x, y, z = (str(v) for v in s['xyz'])
        roll, pitch, yaw = (str(v) for v in s['rpy'])
        nodes.append(Node(
            package='tf2_ros',
            executable='static_transform_publisher',
            name='tf_' + s['frame'],
            arguments=['--x', x, '--y', y, '--z', z,
                       '--roll', roll, '--pitch', pitch, '--yaw', yaw,
                       '--frame-id', s.get('parent', 'base_link'), '--child-frame-id', s['frame']],
        ))
    return LaunchDescription(nodes)
