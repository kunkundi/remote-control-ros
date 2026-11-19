#!/usr/bin/env python3
"""Capture current six-camera shared-memory frames for TensorRT INT8 calibration."""
import argparse
import sys
import time
from pathlib import Path

import cv2
import yaml

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'scripts'))
from camera_shm import CameraShmReader

NAMES = ('front_left', 'front_right', 'front', 'rear', 'left', 'right')
ROOT = Path(__file__).resolve().parents[1] / 'model' / 'exports' / 'calibration'


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--per-camera', type=int, default=60)
    parser.add_argument('--interval', type=float, default=1.0)
    args = parser.parse_args()
    if args.per_camera < 1 or args.interval <= 0:
        parser.error('per-camera and interval must be positive')
    image_dir = ROOT / 'images'
    image_dir.mkdir(parents=True, exist_ok=True)
    readers = {name: CameraShmReader('dozer_camera_' + name, max_age_sec=1.0)
               for name in NAMES}
    counts = {name: 0 for name in NAMES}
    deadline = time.monotonic() + args.per_camera * args.interval * 2 + 30
    while any(count < args.per_camera for count in counts.values()):
        if time.monotonic() > deadline:
            raise TimeoutError(f'Camera capture incomplete: {counts}')
        started = time.monotonic()
        for name, reader in readers.items():
            if counts[name] >= args.per_camera:
                continue
            frame = reader.read()
            if frame is None or frame.frame_id != name:
                continue
            filename = image_dir / f'{name}_{frame.sequence:08d}.jpg'
            if not cv2.imwrite(str(filename), frame.image, [cv2.IMWRITE_JPEG_QUALITY, 90]):
                raise OSError(f'Could not save {filename}')
            counts[name] += 1
        print('captured', counts, flush=True)
        time.sleep(max(0, args.interval - (time.monotonic() - started)))
    dataset = {'path': str(ROOT), 'train': 'images', 'val': 'images',
               'names': {i: f'class_{i}' for i in range(80)}}
    with (ROOT / 'data.yaml').open('w') as handle:
        yaml.safe_dump(dataset, handle, sort_keys=False)
    print(f'DATASET={ROOT / "data.yaml"} IMAGES={sum(counts.values())}')


if __name__ == '__main__':
    main()
