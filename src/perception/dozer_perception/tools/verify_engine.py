#!/usr/bin/env python3
"""Smoke-test a TensorRT engine and report median runtime on a synthetic 640x360 frame."""
import argparse
import statistics
import time
from pathlib import Path

import numpy as np
from ultralytics import YOLO
import torch


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('model', type=Path)
    parser.add_argument('--runs', type=int, default=20)
    parser.add_argument('--batch', type=int, default=1, help='Fixed engine batch size')
    args = parser.parse_args()
    if not args.model.is_file() or args.runs < 1 or args.batch < 1:
        parser.error('model must exist and runs and batch must be positive')
    torch.backends.cudnn.enabled = False
    model = YOLO(str(args.model))
    image = np.zeros((360, 640, 3), dtype=np.uint8)
    images = [image] * args.batch
    for _ in range(3):
        model.predict(images, device=0, verbose=False)
    timings = []
    internal = []
    for _ in range(args.runs):
        started = time.perf_counter()
        result = model.predict(images, device=0, verbose=False)[0]
        timings.append((time.perf_counter() - started) * 1000)
        internal.append(result.speed.get('inference', 0.0))
    print(f'model={args.model} runs={args.runs} boxes_last={len(result.boxes)}')
    print(f'predict_median_ms={statistics.median(timings):.2f} '
          f'predict_mean_ms={statistics.mean(timings):.2f} '
          f'internal_inference_mean_ms={statistics.mean(internal):.2f}')


if __name__ == '__main__':
    main()
