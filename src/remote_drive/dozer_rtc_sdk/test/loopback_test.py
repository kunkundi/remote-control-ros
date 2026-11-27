"""Run two SDK peers with synthetic video, a data channel and a local signaling server."""

import json
import socket
import subprocess
import sys
import tempfile
import time
from pathlib import Path
from urllib.request import urlopen


def run(server, peer, web, workspace):
    with socket.socket() as sock:
        sock.bind(('127.0.0.1', 0))
        port = sock.getsockname()[1]
    endpoint = f'127.0.0.1:{port}'
    (workspace / 'signaling-server.cfg').write_text(json.dumps({'port': port}))
    (workspace / 'web').symlink_to(web, target_is_directory=True)
    children = []
    try:
        children.append(subprocess.Popen([server], cwd=workspace))
        for _ in range(100):
            try:
                with urlopen(f'http://{endpoint}/', timeout=1) as response:
                    assert b'<html>' in response.read().lower()
                break
            except OSError:
                if children[0].poll() is not None:
                    raise RuntimeError('signaling server exited during startup')
                time.sleep(0.05)
        else:
            raise RuntimeError('signaling server did not become ready')

        for role in ('receive', 'send'):
            directory = workspace / role
            directory.mkdir()
            config = dict(api_server=endpoint, signaling_server=endpoint + '/signaling',
                          LAN=True, netstats_report=True, use_NVENC=True, jetson_h264_encoder='jetson', use_NVDEC=False)
            (directory / 'rtc.cfg').write_text(json.dumps(config))
            children.append(subprocess.Popen([peer, role], cwd=directory))
        for process in children[1:]:
            if process.wait(timeout=30):
                raise RuntimeError(f'{process.args[-1]} peer failed')
    finally:
        for process in reversed(children):
            if process.poll() is None:
                process.terminate()
            try:
                process.wait(timeout=3)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait()


if __name__ == '__main__':
    with tempfile.TemporaryDirectory() as directory:
        run(*sys.argv[1:], Path(directory))
