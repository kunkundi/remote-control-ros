#!/usr/bin/env bash
# One-time Jetson Orin / JetPack 6 (CUDA 12.6) GPU Python setup.
set -euo pipefail

workspace="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
venv="$workspace/.venv-gpu"
python3 -m venv --system-site-packages "$venv"
"$venv/bin/python" -m pip install --no-deps \
  --index-url https://pypi.jetson-ai-lab.io/jp6/cu126/+simple \
  'torch==2.8.0' 'torchvision==0.23.0'
"$venv/bin/python" -m pip install --no-deps 'nvidia-cudnn-cu12==9.3.0.75'

export LD_LIBRARY_PATH="$venv/lib/python3.10/site-packages/nvidia/cudnn/lib:${LD_LIBRARY_PATH:-}"
"$venv/bin/python" - <<'PY'
import torch
torch.backends.cudnn.enabled = False
assert torch.cuda.is_available(), 'CUDA unavailable'
result = torch.nn.functional.conv2d(
    torch.ones(1, 1, 4, 4, device='cuda'),
    torch.ones(1, 1, 2, 2, device='cuda'))
assert result.is_cuda
print(f'GPU inference ready: PyTorch {torch.__version__}, {torch.cuda.get_device_name(0)}')
PY
