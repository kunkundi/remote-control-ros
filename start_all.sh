#!/usr/bin/env bash
set -euo pipefail

workspace="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
build=true
dry_run=false
viewer=false
if [[ -n "${DISPLAY:-}" || -n "${WAYLAND_DISPLAY:-}" ]]; then
  viewer=true
fi

usage() {
  printf '%s\n' \
    'Usage: ./start_all.sh [--no-build] [--headless] [--dry-run]' \
    'Start cameras, radar, audio, perception, fusion, static TF and RTC edge.' \
    '  --no-build  Use the existing workspace installation.' \
    '  --headless  Disable the preview window (automatic without a display).' \
    '  --dry-run   Print commands without building or accessing hardware.' \
    '  -h, --help  Show this help.'
}

for argument in "$@"; do
  case "$argument" in
    --no-build) build=false ;;
    --headless) viewer=false ;;
    --dry-run) dry_run=true ;;
    -h|--help) usage; exit 0 ;;
    *) printf 'Unknown argument: %s\n' "$argument" >&2; usage >&2; exit 2 ;;
  esac
done

build_command=(colcon build --symlink-install --packages-up-to
  dozer_audio dozer_camera_gmsl dozer_radar_sr73f dozer_perception
  dozer_fusion dozer_bringup dozer_rtc_edge
  --cmake-args -DCMAKE_BUILD_TYPE=Release)
launch_command=(ros2 launch dozer_bringup dozer.launch.py
  use_camera:=true use_radar:=true use_audio:=true
  use_perception:=true use_fusion:=true "use_viewer:=$viewer" use_rtc_edge:=true)

if [[ "$dry_run" == true ]]; then
  printf 'Workspace: %s\n' "$workspace"
  if [[ "$build" == true ]]; then
    printf '%q ' "${build_command[@]}"
    printf '\n'
  fi
  printf '%q ' "${launch_command[@]}"
  printf '\n'
  exit 0
fi

gpu_python="$workspace/.venv-gpu/bin/python"
model="$workspace/src/perception/dozer_perception/model/best.pt"
cudnn_lib="$workspace/.venv-gpu/lib/python3.10/site-packages/nvidia/cudnn/lib"
if [[ ! -f /opt/ros/humble/setup.bash ]]; then
  printf 'ROS 2 Humble setup not found: /opt/ros/humble/setup.bash\n' >&2
  exit 1
fi
if [[ ! -x "$gpu_python" || ! -f "$model" ]]; then
  printf 'Missing GPU Python environment or YOLO model. Run ./setup_gpu_inference.sh first.\n' >&2
  exit 1
fi
if [[ -d "$cudnn_lib" ]]; then
  export LD_LIBRARY_PATH="$cudnn_lib:${LD_LIBRARY_PATH:-}"
fi
if ! "$gpu_python" -c 'import cv2, numpy, ultralytics, torch; assert torch.cuda.is_available(); torch.backends.cudnn.enabled = False; assert torch.nn.functional.conv2d(torch.ones(1,1,4,4,device="cuda"), torch.ones(1,1,2,2,device="cuda")).is_cuda' >/dev/null 2>&1; then
  printf 'GPU Python cannot execute CUDA convolution. Check PyTorch/cuDNN installation.\n' >&2
  exit 1
fi

set +u
source /opt/ros/humble/setup.bash
set -u
cd "$workspace"
if [[ "$build" == true ]]; then
  "${build_command[@]}"
fi
if [[ ! -f "$workspace/install/setup.bash" ]]; then
  printf 'Workspace installation missing. Run without --no-build first.\n' >&2
  exit 1
fi
set +u
source "$workspace/install/setup.bash"
set -u
export PATH="$workspace/.venv-gpu/bin:$PATH"

printf 'Starting all implemented modules (preview: %s). Press Ctrl+C to stop.\n' "$viewer"
exec "${launch_command[@]}"