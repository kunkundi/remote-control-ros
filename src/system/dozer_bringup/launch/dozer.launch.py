"""autoDozer 车端总启动入口。各模块实现后在此逐个接入。"""

import os
import json
from pathlib import Path

from ament_index_python.packages import get_package_share_directory
from launch import LaunchDescription
from launch.actions import DeclareLaunchArgument, IncludeLaunchDescription, OpaqueFunction
from launch.conditions import IfCondition
from launch.launch_description_sources import PythonLaunchDescriptionSource
from launch.substitutions import LaunchConfiguration
from launch_ros.actions import Node


CAMERA_NAMES = ('front_left', 'front_right', 'front', 'rear', 'left', 'right')


def perception_camera_names(context):
     value = LaunchConfiguration('perception_cameras', default='front_left,front_right').perform(context)
     names = value.split(',')
     if any(name not in CAMERA_NAMES for name in names) or len(names) != len(set(names)):
          raise RuntimeError('perception_cameras must contain unique comma-separated camera names: '
                                 + ','.join(CAMERA_NAMES))
     return names


def create_perception_nodes(context):
     names = perception_camera_names(context)
     if not IfCondition(LaunchConfiguration('use_perception')).evaluate(context):
          return []
     parameters = os.path.join(get_package_share_directory('dozer_bringup'),
                                     'config', 'perception.yaml')
     return [Node(package='dozer_perception', executable='yolo_node.py', name='yolo_' + name,
                     parameters=[parameters, {'image_backend': LaunchConfiguration('image_backend')}])
               for name in names]


def validate_camera_backend(context):
     selected = perception_camera_names(context)
     backend = LaunchConfiguration('image_backend').perform(context)
     cameras = any(LaunchConfiguration(name).perform(context).lower() in ('true', '1')
                      for name in ('use_camera', 'use_mock_camera'))
     if backend == 'shm' and cameras:
          raise RuntimeError('Shared-memory mode uses edge direct capture; disable use_camera/use_mock_camera')
     if LaunchConfiguration('use_rtc_edge', default='false').perform(context).lower() in ('true', '1'):
          config_path = Path(LaunchConfiguration('rtc_config').perform(context))
          edge = json.loads(config_path.read_text()).get('edge', {})
          if 'config' in edge:
               edge = json.loads((config_path.parent / edge['config']).read_text())
          stereo = edge.get('stereo_camera', {})
          devices = list(stereo.values()) + list(edge.get('surround_camera', {}).values())
          if cameras and any(str(device).startswith('/dev/') for device in devices):
               raise RuntimeError('ROS cameras and edge direct capture cannot own the same devices')
          if backend == 'shm' and not all(str(stereo.get(key, '')).startswith('/dev/video')
                                                  for key in ('left_device', 'right_device')):
               raise RuntimeError('Shared-memory mode requires direct stereo devices in edge config')
          if backend == 'shm' and IfCondition(LaunchConfiguration('use_perception', default='false')).evaluate(context):
               devices_by_camera = {'front_left': stereo.get('left_device', ''),
                                         'front_right': stereo.get('right_device', '')}
               devices_by_camera.update({name: edge.get('surround_camera', {}).get(name + '_device', '')
                                               for name in CAMERA_NAMES[2:]})
               for name in selected:
                    if not str(devices_by_camera[name]).startswith('/dev/video'):
                         raise RuntimeError('Shared-memory perception requires a direct device for ' + name)
     return []


def generate_launch_description():
    bringup = get_package_share_directory('dozer_bringup')
    use_mock_radar = LaunchConfiguration('use_mock_radar')
    use_mock_camera = LaunchConfiguration('use_mock_camera')
    use_camera = LaunchConfiguration('use_camera')
    use_radar = LaunchConfiguration('use_radar')
    use_fusion = LaunchConfiguration('use_fusion')
    use_viewer = LaunchConfiguration('use_viewer')

    return LaunchDescription([
        DeclareLaunchArgument('use_mock_radar', default_value='false',
                              description='使用模拟雷达数据代替真实驱动'),
        DeclareLaunchArgument('use_mock_camera', default_value='false',
                              description='发布三路模拟图像以检查感知话题链路'),
        DeclareLaunchArgument('use_camera', default_value='false',
                              description='启动六路真实 V4L2 相机采集节点'),
     DeclareLaunchArgument('image_backend', default_value='shm', choices=['shm', 'ros'],
                     description='感知图像输入：edge 共享内存或兼容 ROS 话题'),
     DeclareLaunchArgument('use_audio', default_value='false',
                     description='启动 PCM 音频采集节点'),
        DeclareLaunchArgument('use_radar', default_value='false',
                              description='启动 SR73F 雷达驱动（参数见 config/radar.yaml）'),
          DeclareLaunchArgument('use_rtc_edge', default_value='false',
                                     description='启动远程驾驶 RTC edge'),
          DeclareLaunchArgument('rtc_config', default_value=os.path.join(
               bringup, 'config', 'rtc_edge', 'rtc.cfg')),
        DeclareLaunchArgument('use_perception', default_value='false',
                              description='启动 YOLO 图像检测'),
     DeclareLaunchArgument('perception_cameras', default_value='front_left,front_right',
                     description='YOLO 相机名单，逗号分隔，可选 front_left,front_right,front,rear,left,right'),
        DeclareLaunchArgument('use_fusion', default_value='false',
                              description='启动雷达相机融合'),
        DeclareLaunchArgument('use_viewer', default_value='false',
                              description='弹出两路检测画面，默认不发布图像话题'),
          DeclareLaunchArgument('use_static_tf', default_value='true',
                                     description='启动静态坐标变换发布节点'),
                    OpaqueFunction(function=validate_camera_backend),

        IncludeLaunchDescription(PythonLaunchDescriptionSource(
               os.path.join(bringup, 'launch', 'static_tf.launch.py')),
               condition=IfCondition(LaunchConfiguration('use_static_tf'))),

        Node(package='dozer_tools', executable='mock_radar_publisher.py',
             name='mock_radar_publisher', condition=IfCondition(use_mock_radar)),
        Node(package='dozer_tools', executable='mock_camera_publisher.py',
             name='mock_camera_publisher', condition=IfCondition(use_mock_camera)),

        # 驱动层
        IncludeLaunchDescription(
            PythonLaunchDescriptionSource(os.path.join(bringup, 'launch', 'radar.launch.py')),
            condition=IfCondition(use_radar)),
          IncludeLaunchDescription(
               PythonLaunchDescriptionSource(os.path.join(bringup, 'launch', 'audio.launch.py')),
               condition=IfCondition(LaunchConfiguration('use_audio'))),

        *[Node(package='dozer_camera_gmsl', executable='camera_gmsl_node',
               name='camera_' + name,
               parameters=[os.path.join(bringup, 'config', 'camera.yaml')],
                  condition=IfCondition(use_camera)) for name in (
                       'front_left', 'front_right', 'front', 'rear', 'left', 'right')],

       OpaqueFunction(function=create_perception_nodes),
        Node(package='dozer_fusion', executable='fusion_node.py', name='fusion_node',
             parameters=[os.path.join(bringup, 'config', 'fusion.yaml')],
             condition=IfCondition(use_fusion)),
        Node(package='dozer_perception', executable='detection_viewer.py',
             name='detection_viewer', condition=IfCondition(use_viewer),
             parameters=[{'image_backend': LaunchConfiguration('image_backend')}]),
        Node(package='dozer_rtc_edge', executable='rtc_edge', name='rtc_edge',
             output='screen',
             arguments=['--config', LaunchConfiguration('rtc_config')],
             condition=IfCondition(LaunchConfiguration('use_rtc_edge'))),

        # TODO：其它模块实现后依次接入，参数统一从 config/*.yaml 读取。
        # 感知与控制
        # Node(package='dozer_perception', executable='perception_node', ...),
        # Node(package='dozer_vehicle', executable='vehicle_node', ...),
    ])
