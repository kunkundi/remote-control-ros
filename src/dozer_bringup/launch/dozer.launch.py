"""autoDozer 车端总启动入口。各模块实现后在此逐个接入。"""

import os

from ament_index_python.packages import get_package_share_directory
from launch import LaunchDescription
from launch.actions import DeclareLaunchArgument, IncludeLaunchDescription
from launch.conditions import IfCondition
from launch.launch_description_sources import PythonLaunchDescriptionSource
from launch.substitutions import LaunchConfiguration
from launch_ros.actions import Node


def generate_launch_description():
    bringup = get_package_share_directory('dozer_bringup')
    use_mock_radar = LaunchConfiguration('use_mock_radar')

    return LaunchDescription([
        DeclareLaunchArgument('use_mock_radar', default_value='false',
                              description='使用模拟雷达数据代替真实驱动'),

        IncludeLaunchDescription(PythonLaunchDescriptionSource(
            os.path.join(bringup, 'launch', 'static_tf.launch.py'))),

        Node(package='dozer_tools', executable='mock_radar_publisher.py',
             name='mock_radar_publisher', condition=IfCondition(use_mock_radar)),

        # TODO：模块实现后依次接入，参数统一从 config/*.yaml 读取。
        # Node(package='dozer_radar', executable='radar_node', name='dozer_radar',
        #      parameters=[os.path.join(bringup, 'config', 'radar.yaml')]),
        # Node(package='dozer_perception', executable='perception_node', ...),
        # Node(package='dozer_vehicle', executable='vehicle_node', ...),
        # Node(package='dozer_rtc_bridge', executable='rtc_bridge_node', ...),
    ])
