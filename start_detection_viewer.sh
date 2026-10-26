#!/usr/bin/env bash
# 在本机图形桌面启动相机、毫米波雷达、融合和带框预览窗口。
set -euo pipefail

workspace="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
model="$workspace/src/perception/dozer_perception/model/best.pt"
gpu_python="$workspace/.venv-gpu/bin/python"
cudnn_lib="$workspace/.venv-gpu/lib/python3.10/site-packages/nvidia/cudnn/lib"

if [[ -z "${DISPLAY:-}" && -z "${WAYLAND_DISPLAY:-}" ]]; then
  echo "未检测到图形桌面环境。请在机器的桌面终端运行此脚本，并确认 DISPLAY 或 WAYLAND_DISPLAY 已设置。" >&2
  exit 1
fi
if [[ ! -f "$model" ]]; then
  echo "缺少 YOLO 模型：$model" >&2
  exit 1
fi
for device in /dev/video0 /dev/video1; do
  if [[ ! -r "$device" || ! -w "$device" ]]; then
    echo "相机设备不存在或当前用户无权访问：$device" >&2
    exit 1
  fi
done
if [[ ! -x "$gpu_python" ]]; then
  echo "缺少工程内的 GPU Python 环境：$gpu_python；请先运行 ./setup_gpu_inference.sh" >&2
  exit 1
fi
if [[ -d "$cudnn_lib" ]]; then
  export LD_LIBRARY_PATH="$cudnn_lib:${LD_LIBRARY_PATH:-}"
fi
if ! "$gpu_python" -c 'import cv2, numpy, ultralytics, torch; assert torch.cuda.is_available(); torch.backends.cudnn.enabled = False; assert torch.nn.functional.conv2d(torch.ones(1,1,4,4,device="cuda"), torch.ones(1,1,2,2,device="cuda")).is_cuda' >/dev/null 2>&1; then
  echo "GPU Python 环境无法执行 CUDA 卷积；请检查 PyTorch/cuDNN 安装。" >&2
  exit 1
fi

# ROS setup 脚本可能引用未定义变量，暂时关闭 nounset。
set +u
source /opt/ros/humble/setup.bash
set -u
cd "$workspace"
colcon build --symlink-install --packages-select \
  dozer_msgs dozer_camera_gmsl dozer_radar_sr73f \
  dozer_perception dozer_fusion dozer_bringup
set +u
source "$workspace/install/setup.bash"
set -u
export PATH="$workspace/.venv-gpu/bin:$PATH"

echo "正在启动相机检测窗口和前左雷达实时俯视图；按 Ctrl+C 停止。"
exec ros2 launch dozer_bringup dozer.launch.py \
  use_camera:=true use_radar:=true use_perception:=true \
  use_fusion:=true use_viewer:=true
