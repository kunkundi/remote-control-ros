"""在本机信令服务上反复关闭和重开房间，验证真实 edge 能恢复双目与环视视频。"""

from contextlib import ExitStack, contextmanager
import json
import os
from pathlib import Path
import signal
import socket
import subprocess
import sys
import tempfile
import time

from ament_index_python.packages import get_package_prefix, get_package_share_directory
import rclpy
from rclpy.qos import qos_profile_sensor_data
from sensor_msgs.msg import Image


@contextmanager
def child(command, directory):
    process = subprocess.Popen(command, cwd=directory)
    try:
        yield process
    finally:
        if process.poll() is None:
            process.send_signal(signal.SIGTERM)
            try:
                process.wait(timeout=3)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait()


def run(edge_binary, receiver, directory):
    with socket.socket() as sock:
        sock.bind(('127.0.0.1', 0))
        port = sock.getsockname()[1]
    endpoint = f'127.0.0.1:{port}'
    sdk = 'dozer_rtc_sdk'
    server = Path(get_package_prefix(sdk)) / 'lib' / sdk / 'signaling-server'
    (directory / 'web').symlink_to(Path(get_package_share_directory(sdk)) / 'web')
    (directory / 'signaling-server.cfg').write_text(json.dumps({'port': port}))
    rtc = dict(api_server=endpoint, signaling_server=endpoint + '/signaling',
               LAN=True, use_NVENC=False, use_NVDEC=False)
    (directory / 'rtc.cfg').write_text(json.dumps(rtc))
    topic = '/edge_reconnect_test/image'
    options = dict(rtc=dict(room='edge-reconnect-test', join_retry_ms=100, status_interval_sec=1,
                            frame_limit=0),
                   stereo_camera=dict(left_device=topic, right_device=topic),
                   surround_camera=dict(front_device=topic, rear_device=topic, left_device='', right_device=''),
                   vehicle_control=dict(debug_mode=True, watchdog_ms=300, state_interval_ms=50,
                                        max_pending_events=128))
    config = directory / 'edge.cfg'
    config.write_text(json.dumps(dict(rtc, edge=options)))
    os.environ['ROS_LOG_DIR'] = str(directory / 'ros_logs')
    with ExitStack() as stack:
        server_process = stack.enter_context(child([str(server)], directory))
        for _ in range(100):
            try:
                with socket.create_connection(('127.0.0.1', port), timeout=0.1):
                    break
            except OSError:
                if server_process.poll() is not None:
                    raise RuntimeError('信令服务提前退出')
                time.sleep(0.05)
        else:
            raise RuntimeError('信令服务启动超时')

        rclpy.init(args=[])
        stack.callback(rclpy.shutdown)
        node = rclpy.create_node('edge_reconnect_test')
        stack.callback(node.destroy_node)
        publisher = node.create_publisher(Image, topic, qos_profile_sensor_data)
        image = Image(height=90, width=160, step=480, encoding='bgr8', data=bytes([96]) * 43200)
        edge = stack.enter_context(child([edge_binary, '--config', str(config)], directory))
        peer = stack.enter_context(child([receiver], directory))
        deadline = time.monotonic() + 40
        while peer.poll() is None:
            if edge.poll() is not None or time.monotonic() >= deadline:
                raise RuntimeError('edge 提前退出或重连超时')
            image.header.stamp = node.get_clock().now().to_msg()
            publisher.publish(image)
            time.sleep(1 / 30)
        if peer.returncode:
            raise RuntimeError('重开房间后未收到双目和环视图像')
        edge.send_signal(signal.SIGINT)
        if edge.wait(timeout=5):
            raise RuntimeError('edge 未正常退出')


if __name__ == '__main__':
    with tempfile.TemporaryDirectory() as work:
        run(*sys.argv[1:], Path(work))
