#!/usr/bin/env python3
"""Compare checkpoint, FP16, and INT8 detection counts on sampled calibration frames."""
import gc
from pathlib import Path

import cv2
from ultralytics import YOLO
import torch

ROOT = Path(__file__).resolve().parents[1] / 'model' / 'exports'
NAMES = ('front_left', 'front_right', 'front', 'rear', 'left', 'right')


def sample_images():
    images = []
    for name in NAMES:
        files = sorted((ROOT / 'calibration/images').glob(name + '_*.jpg'))
        if name == 'front':
            files = [p for p in files if p.name.startswith('front_')]
        if len(files) < 2:
            raise RuntimeError(f'Insufficient images for {name}')
        images += [files[len(files) // 3], files[2 * len(files) // 3]]
    return images


def main():
    torch.backends.cudnn.enabled = False
    images = sample_images()
    for label, filename in (('pt', 'best_fp16.pt'), ('fp16', 'best_fp16.engine'),
                            ('int8', 'best_int8.engine')):
        model = YOLO(str(ROOT / filename))
        counts = []
        for image in images:
            result = model.predict(cv2.imread(str(image)), device=0, conf=0.25,
                                   verbose=False)[0]
            counts.append(len(result.boxes))
        print(label, 'total_boxes', sum(counts), 'per_image', counts, flush=True)
        del model
        gc.collect()
        torch.cuda.empty_cache()


if __name__ == '__main__':
    main()
