"""启动 SR73F 雷达驱动。

  ros2 launch dozer_bringup radar.launch.py
  ros2 launch dozer_bringup radar.launch.py replay_file:=/path/sim.log
"""

import os

from ament_index_python.packages import get_package_share_directory
from launch import LaunchDescription
from launch.actions import DeclareLaunchArgument
from launch.substitutions import LaunchConfiguration
from launch_ros.actions import Node


def generate_launch_description():
    params = os.path.join(get_package_share_directory('dozer_bringup'), 'config', 'radar.yaml')
    return LaunchDescription([
        DeclareLaunchArgument('can_interface', default_value='can0'),
        DeclareLaunchArgument('replay_file', default_value='',
                              description='candump -l 日志路径，非空时回放而不读 CAN'),
        Node(
            package='dozer_radar_sr73f',
            executable='radar_sr73f_node',
            name='radar_sr73f',
            output='screen',
            parameters=[params, {
                'can_interface': LaunchConfiguration('can_interface'),
                'replay_file': LaunchConfiguration('replay_file'),
            }],
        ),
    ])
