"""Launch the PCM audio capture node."""

import os

from ament_index_python.packages import get_package_share_directory
from launch import LaunchDescription
from launch.actions import DeclareLaunchArgument
from launch.substitutions import LaunchConfiguration
from launch_ros.actions import Node


def generate_launch_description():
    params = os.path.join(get_package_share_directory('dozer_bringup'), 'config', 'audio.yaml')
    return LaunchDescription([
        DeclareLaunchArgument('params_file', default_value=params),
        Node(package='dozer_audio', executable='audio_capture_node',
             name='audio_capture', output='screen',
             parameters=[LaunchConfiguration('params_file')]),
    ])