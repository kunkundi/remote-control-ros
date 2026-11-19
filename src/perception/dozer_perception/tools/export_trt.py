#!/usr/bin/env python3
"""Export the workspace YOLO11n checkpoint to FP16 or calibrated INT8 TensorRT."""
import argparse
import shutil
from pathlib import Path

from ultralytics import YOLO
import torch

MODEL_DIR = Path(__file__).resolve().parents[1] / 'model'
SOURCE = MODEL_DIR / 'best.pt'
OUTPUT = MODEL_DIR / 'exports'


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('precision', choices=('fp16', 'int8'))
    parser.add_argument('--data', type=Path, help='Ultralytics dataset YAML with representative val images')
    parser.add_argument('--workspace', type=float, default=1.0, help='TensorRT builder workspace in GiB')
    parser.add_argument('--batch', type=int, default=1, help='Fixed TensorRT batch size')
    parser.add_argument('--nms', action='store_true', help='Embed NMS into the TensorRT engine')
    args = parser.parse_args()
    if args.batch < 1:
        parser.error('batch must be positive')
    if args.precision == 'int8' and (args.data is None or not args.data.is_file()):
        parser.error('INT8 requires --data pointing to an existing dataset YAML')
    if not SOURCE.is_file():
        parser.error(f'model missing: {SOURCE}')
    OUTPUT.mkdir(parents=True, exist_ok=True)
    suffix = f'_batch{args.batch}' if args.batch != 1 else ''
    if args.nms:
        suffix += '_nms'
    checkpoint = OUTPUT / f'best_{args.precision}{suffix}.pt'
    shutil.copy2(SOURCE, checkpoint)
    if not torch.cuda.is_available():
        raise RuntimeError('CUDA is unavailable')
    torch.backends.cudnn.enabled = False
    options = dict(format='engine', device=0, imgsz=640, batch=args.batch,
                   workspace=args.workspace, simplify=True, nms=args.nms)
    if args.precision == 'fp16':
        options['quantize'] = 16
    else:
        options.update(quantize=8, data=str(args.data.resolve()))
    engine = Path(YOLO(str(checkpoint)).export(**options))
    print(f'ENGINE={engine} SIZE_BYTES={engine.stat().st_size}')


if __name__ == '__main__':
    main()
