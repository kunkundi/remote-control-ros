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
    use_mock_camera = LaunchConfiguration('use_mock_camera')
    use_camera = LaunchConfiguration('use_camera')
    use_radar = LaunchConfiguration('use_radar')
    use_perception = LaunchConfiguration('use_perception')
    use_fusion = LaunchConfiguration('use_fusion')
    use_viewer = LaunchConfiguration('use_viewer')

    return LaunchDescription([
        DeclareLaunchArgument('use_mock_radar', default_value='false',
                              description='使用模拟雷达数据代替真实驱动'),
        DeclareLaunchArgument('use_mock_camera', default_value='false',
                              description='发布三路模拟图像以检查感知话题链路'),
        DeclareLaunchArgument('use_camera', default_value='false',
                              description='启动两路真实 V4L2 相机采集节点'),
        DeclareLaunchArgument('use_radar', default_value='false',
                              description='启动 SR73F 雷达驱动（参数见 config/radar.yaml）'),
        DeclareLaunchArgument('use_perception', default_value='false',
                              description='启动 YOLO 图像检测'),
        DeclareLaunchArgument('use_fusion', default_value='false',
                              description='启动雷达相机融合'),
        DeclareLaunchArgument('use_viewer', default_value='false',
                              description='弹出两路检测画面，并发布带框图像话题'),

        IncludeLaunchDescription(PythonLaunchDescriptionSource(
            os.path.join(bringup, 'launch', 'static_tf.launch.py'))),

        Node(package='dozer_tools', executable='mock_radar_publisher.py',
             name='mock_radar_publisher', condition=IfCondition(use_mock_radar)),
        Node(package='dozer_tools', executable='mock_camera_publisher.py',
             name='mock_camera_publisher', condition=IfCondition(use_mock_camera)),

        # 驱动层
        IncludeLaunchDescription(
            PythonLaunchDescriptionSource(os.path.join(bringup, 'launch', 'radar.launch.py')),
            condition=IfCondition(use_radar)),

        *[Node(package='dozer_camera_gmsl', executable='camera_gmsl_node',
               name='camera_' + name,
               parameters=[os.path.join(bringup, 'config', 'camera.yaml')],
               condition=IfCondition(use_camera)) for name in ('front_left', 'front_right')],

        *[Node(package='dozer_perception', executable='yolo_node.py', name='yolo_' + name,
               parameters=[os.path.join(bringup, 'config', 'perception.yaml')],
               condition=IfCondition(use_perception)) for name in ('front_left', 'front_right')],
        Node(package='dozer_fusion', executable='fusion_node.py', name='fusion_node',
             parameters=[os.path.join(bringup, 'config', 'fusion.yaml')],
             condition=IfCondition(use_fusion)),
        Node(package='dozer_perception', executable='detection_viewer.py',
             name='detection_viewer', condition=IfCondition(use_viewer)),

        # TODO：其它模块实现后依次接入，参数统一从 config/*.yaml 读取。
        # 感知与控制
        # Node(package='dozer_perception', executable='perception_node', ...),
        # Node(package='dozer_vehicle', executable='vehicle_node', ...),
    ])
