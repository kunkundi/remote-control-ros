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
    use_radar = LaunchConfiguration('use_radar')

    return LaunchDescription([
        DeclareLaunchArgument('use_mock_radar', default_value='false',
                              description='使用模拟雷达数据代替真实驱动'),
        DeclareLaunchArgument('use_radar', default_value='false',
                              description='启动 SR73F 雷达驱动（参数见 config/radar.yaml）'),

        IncludeLaunchDescription(PythonLaunchDescriptionSource(
            os.path.join(bringup, 'launch', 'static_tf.launch.py'))),

        Node(package='dozer_tools', executable='mock_radar_publisher.py',
             name='mock_radar_publisher', condition=IfCondition(use_mock_radar)),

        # 驱动层
        IncludeLaunchDescription(
            PythonLaunchDescriptionSource(os.path.join(bringup, 'launch', 'radar.launch.py')),
            condition=IfCondition(use_radar)),

        # TODO：其它模块实现后依次接入，参数统一从 config/*.yaml 读取。
        # 相机与 RTC 网关在同一 component container 中（第二阶段），保证进程内零拷贝：
        # ComposableNodeContainer(name='video_container', package='rclcpp_components',
        #     executable='component_container_mt', composable_node_descriptions=[
        #         ComposableNode(package='dozer_camera_gmsl', plugin='dozer_camera_gmsl::CameraNode',
        #                        name='camera_gmsl', parameters=[os.path.join(bringup, 'config', 'camera.yaml')],
        #                        extra_arguments=[{'use_intra_process_comms': True}]),
        #         ComposableNode(package='dozer_rtc_bridge', plugin='dozer_rtc_bridge::RtcBridgeNode',
        #                        name='rtc_bridge', extra_arguments=[{'use_intra_process_comms': True}]),
        #     ]),
        # 感知与控制
        # Node(package='dozer_perception', executable='perception_node', ...),
        # Node(package='dozer_vehicle', executable='vehicle_node', ...),
    ])
