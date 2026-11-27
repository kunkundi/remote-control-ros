import importlib.util
import json
from pathlib import Path
import shlex
import subprocess
import tempfile
import unittest
from unittest.mock import patch

import yaml
from launch import LaunchContext


PACKAGE = Path(__file__).resolve().parents[1]
WORKSPACE = PACKAGE.parents[2]
SPEC = importlib.util.spec_from_file_location('dozer_launch', PACKAGE / 'launch/dozer.launch.py')
LAUNCH = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(LAUNCH)


class PerceptionLaunchTests(unittest.TestCase):
    def context(self, selection='front_left,front_right'):
        context = LaunchContext()
        context.launch_configurations.update(
            perception_cameras=selection, use_perception='true', image_backend='shm',
            use_camera='false', use_mock_camera='false', use_rtc_edge='true',
            rtc_config=str(PACKAGE / 'config/rtc_edge/rtc.cfg'))
        return context

    def test_node_selection_and_parameters(self):
        LAUNCH.generate_launch_description()
        parameters = yaml.safe_load((PACKAGE / 'config/perception.yaml').read_text())
        for selection in ('front_left,front_right', 'rear', ','.join(LAUNCH.CAMERA_NAMES)):
            with self.subTest(selection=selection):
                context = self.context(selection)
                LAUNCH.validate_camera_backend(context)
                with patch.object(LAUNCH, 'Node', side_effect=lambda **values: values):
                    batch = LAUNCH.create_perception_nodes(context)
                    self.assertEqual([node['name'] for node in batch], ['yolo_batch'])
                    self.assertEqual(batch[0]['arguments'], ['--camera-names=' + selection])
                    context.launch_configurations['image_backend'] = 'ros'
                    nodes = LAUNCH.create_perception_nodes(context)
                self.assertEqual([node['name'] for node in nodes],
                                 ['yolo_' + name for name in selection.split(',')])
                for name in selection.split(','):
                    configured = parameters['yolo_' + name]['ros__parameters']
                    self.assertEqual(configured['shm_name'], 'dozer_camera_' + name)
                    self.assertEqual(configured['source_id'], name)
                    self.assertEqual(configured['image_topic'], '/camera/' + name + '/image_small')
                    self.assertEqual(configured['detections_topic'], '/perception/' + name + '/detections')
                    self.assertEqual(configured['image_backend'], 'shm')

    def test_disabled_and_default(self):
        context = self.context()
        del context.launch_configurations['perception_cameras']
        self.assertEqual(LAUNCH.perception_camera_names(context), ['front_left', 'front_right'])
        context.launch_configurations['use_perception'] = 'false'
        self.assertEqual(LAUNCH.create_perception_nodes(context), [])

    def test_image_topics_require_shared_memory(self):
        context = self.context()
        context.launch_configurations.update(use_camera_topics='true', use_perception='false',
                                             use_rtc_edge='false')
        LAUNCH.validate_camera_backend(context)
        context.launch_configurations.update(image_backend='ros', use_mock_camera='true')
        with self.assertRaisesRegex(RuntimeError, 'use_camera_topics'):
            LAUNCH.validate_camera_backend(context)

    def test_invalid_lists(self):
        for selection in ('', 'front,front', 'unknown', 'front,', 'front,,rear', 'front, rear'):
            with self.subTest(selection=selection), self.assertRaises(RuntimeError):
                LAUNCH.perception_camera_names(self.context(selection))

    def test_missing_direct_device(self):
        with tempfile.TemporaryDirectory() as directory:
            config = Path(directory) / 'rtc.json'
            config.write_text(json.dumps({'edge': {
                'stereo_camera': {'left_device': '/dev/video0', 'right_device': '/dev/video1'},
                'surround_camera': {'rear_device': '/camera/rear/image_small'}}}))
            context = self.context('rear')
            context.launch_configurations['rtc_config'] = str(config)
            with self.assertRaisesRegex(RuntimeError, 'direct device for rear'):
                LAUNCH.validate_camera_backend(context)
            context.launch_configurations['image_backend'] = 'ros'
            LAUNCH.validate_camera_backend(context)

    def test_script_arguments(self):
        script = str(WORKSPACE / 'start_all.sh')
        for selection in ('front_left,front_right', 'rear', ','.join(LAUNCH.CAMERA_NAMES)):
            result = subprocess.run(
                [script, '--restart', '--dry-run', '--no-build', '--perception-cameras', selection],
                capture_output=True, text=True, check=True)
            arguments = shlex.split(result.stdout.splitlines()[-1])
            self.assertIn('perception_cameras:=' + selection, arguments)
            self.assertIn('use_camera_topics:=true', arguments)
        edge_only = subprocess.check_output([script, '--edge-only', '--restart', '--dry-run'], text=True)
        self.assertNotIn('use_camera_topics:=true', edge_only)
        for arguments in (['--perception-cameras'], ['--perception-cameras', ''],
                          ['--perception-cameras', 'front,front'], ['--perception-cameras', 'unknown'],
                          ['--perception-cameras', 'front,'], ['--perception-cameras', 'front,,rear'],
                          ['--edge-only', '--perception-cameras', 'rear']):
            result = subprocess.run([script, '--dry-run', '--no-build', *arguments],
                                    capture_output=True, text=True)
            self.assertEqual(result.returncode, 2, result.stdout + result.stderr)

    def test_keep_matches_identity_not_count(self):
        script = (WORKSPACE / 'start_all.sh').read_text()
        functions = 'perception_matches_selection() {' + script.split(
            'perception_matches_selection() {', 1)[1].split('\nstop_running_modules()', 1)[0]
        cases = [
            ('front_left,front_right', {'front_left': 1, 'front_right': 1}, 'keep', True),
            ('rear,front', {'front': 1, 'rear': 1}, 'keep', True),
            ('front_left,front_right', {'front_left': 1, 'front_right': 1, 'rear': 1}, 'keep', False),
            ('front_left,front_right', {'rear': 1, 'front': 1}, 'keep', False),
            ('front_left,front_right', {'front_left': 2, 'front_right': 1}, 'keep', False),
            ('front_left,front_right', {'front_left': 1}, 'keep', False),
            ('rear', {'unknown': 1}, 'keep', False),
            (','.join(LAUNCH.CAMERA_NAMES), {name: 1 for name in LAUNCH.CAMERA_NAMES}, 'keep', True),
            (','.join(LAUNCH.CAMERA_NAMES), {'front_left': 1, 'front_right': 1}, 'keep', False),
            (','.join(LAUNCH.CAMERA_NAMES), {'front_left': 1, 'front_right': 1}, 'restart', True),
        ]
        for selection, running, policy, accepted in cases:
            with self.subTest(selection=selection, running=running, policy=policy):
                initializers = ' '.join(f'[{shlex.quote(name)}]={count}' for name, count in running.items())
                program = (
                    'set -euo pipefail\n' + functions + '\nfind_running_modules() { :; }\n'
                    'modules=(perception)\ndeclare -A running=([perception]="101 ")\n'
                    'radar_names=front_left\nrunning_perception_mode=batch\n'
                    f'declare -A running_perception=({initializers})\n'
                    f'perception_cameras={shlex.quote(selection)}\n'
                    'IFS=, read -r -a selected_perception_cameras <<< "$perception_cameras"\n'
                    f'existing={policy}\nedge_only=false\nviewer=false\n'
                    'prepare_launch\nprintf "%s\\n" "${launch_command[@]}"\n')
                result = subprocess.run(['bash', '-s'], input=program,
                                        capture_output=True, text=True)
                self.assertEqual(result.returncode == 0, accepted, result.stdout + result.stderr)
                if accepted:
                    enabled = 'true' if policy == 'restart' else 'false'
                    self.assertIn('use_perception:=' + enabled, result.stdout)
                else:
                    self.assertIn('--restart', result.stderr)

    def test_process_camera_identity(self):
        script = (WORKSPACE / 'start_all.sh').read_text()
        function = 'find_running_modules() {' + script.split(
            'find_running_modules() {', 1)[1].split('\nperception_matches_selection()', 1)[0]
        executable = str(WORKSPACE / 'install/dozer_perception/lib/dozer_perception/yolo_node.py')
        with tempfile.TemporaryDirectory() as directory:
            for index, name in enumerate(('front_left', 'front_left', 'rear', 'unknown', 'foreign')):
                process = Path(directory) / str(100 + index)
                process.mkdir()
                command = '/other' + executable if name == 'foreign' else executable
                arguments = ['/usr/bin/python3', command, '--ros-args', '-r', '__node:=yolo_' + name]
                (process / 'cmdline').write_bytes(b'\0'.join(value.encode() for value in arguments) + b'\0')
            program = (
                'set -euo pipefail\n' + function.replace('/proc/', directory + '/')
                + f'\nworkspace={shlex.quote(str(WORKSPACE))}\nmodules=(perception)\n'
                'declare -A running=() running_perception=()\nfind_running_modules\n'
                'for camera_name in "${!running_perception[@]}"; do\n'
                'printf "%s:%s\\n" "$camera_name" "${running_perception[$camera_name]}"\ndone\n')
            result = subprocess.run(['bash', '-s'], input=program,
                                    capture_output=True, text=True, check=True)
            self.assertEqual(set(result.stdout.splitlines()), {'front_left:2', 'rear:1', 'unknown:1'})

    def test_keep_requires_selected_shared_files(self):
        script = (WORKSPACE / 'start_all.sh').read_text()
        function = 'prepare_launch() {' + script.split(
            'prepare_launch() {', 1)[1].split('\nstop_running_modules()', 1)[0]
        with tempfile.TemporaryDirectory() as directory:
            program = (
                'set -euo pipefail\n' + function.replace('/dev/shm/', directory + '/')
                + '\nfind_running_modules() { :; }\nmodules=(rtc)\n'
                'radar_names=front_left\ndeclare -A running=([rtc]=101)\nexisting=keep\nedge_only=false\nviewer=false\n'
                f'perception_cameras={shlex.quote(",".join(LAUNCH.CAMERA_NAMES))}\n'
                'IFS=, read -r -a selected_perception_cameras <<< "$perception_cameras"\n'
                'prepare_launch\n')
            for name in LAUNCH.CAMERA_NAMES:
                (Path(directory) / ('dozer_camera_' + name)).touch()
            result = subprocess.run(['bash', '-s'], input=program,
                                    capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            for name in LAUNCH.CAMERA_NAMES:
                with self.subTest(missing=name):
                    snapshot = Path(directory) / ('dozer_camera_' + name)
                    snapshot.unlink()
                    result = subprocess.run(['bash', '-s'], input=program,
                                            capture_output=True, text=True)
                    self.assertNotEqual(result.returncode, 0)
                    self.assertIn('--restart', result.stderr)
                    snapshot.touch()


if __name__ == '__main__':
    unittest.main()
