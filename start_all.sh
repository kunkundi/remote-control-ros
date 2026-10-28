#!/usr/bin/env bash
set -euo pipefail

workspace="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
build=true
dry_run=false
viewer=false
existing=keep
policy_argument=''
if [[ -n "${DISPLAY:-}" || -n "${WAYLAND_DISPLAY:-}" ]]; then
  viewer=true
fi

usage() {
  printf '%s\n' \
    'Usage: ./start_all.sh [--keep|--restart] [--no-build] [--headless] [--dry-run]' \
    'Start cameras, radar, audio, perception, fusion, static TF and RTC edge.' \
    '  --no-build  Use the existing workspace installation.' \
    '  --headless  Disable the preview window (automatic without a display).' \
    '  --dry-run   Print commands without building or accessing hardware.' \
    '  --keep     Keep running modules and start missing modules (default).' \
    '  --restart  Stop existing workspace modules before starting all modules.' \
    '  -h, --help  Show this help.'
}

argument_error() {
  printf '%s\n' "$1" >&2
  usage >&2
  exit 2
}

while (( $# > 0 )); do
  case "$1" in
    --no-build) build=false ;;
    --headless) viewer=false ;;
    --dry-run) dry_run=true ;;
    --restart|--keep)
      [[ -z "$policy_argument" || "$policy_argument" == "$1" ]] || argument_error '--keep and --restart cannot be used together.'
      existing="${1#--}"
      policy_argument="$1"
      ;;
    -h|--help) usage; exit 0 ;;
    *) argument_error "Unknown argument: $1" ;;
  esac
  shift
done

modules=(camera radar audio perception fusion tf rtc viewer)
declare -A running=()

find_running_modules() {
  running=()
  local process process_id module argument parent_id field value remainder
  local -a command_args parent_args
  for process in /proc/[0-9]*; do
    [[ -O "$process" && -r "$process/cmdline" ]] || continue
    command_args=()
    mapfile -d '' -t command_args < "$process/cmdline" 2>/dev/null || continue
    module=''
    for argument in "${command_args[@]:0:2}"; do
      case "$argument" in
        "$workspace/install/dozer_camera_gmsl/lib/dozer_camera_gmsl/camera_gmsl_node") module=camera ;;
        "$workspace/install/dozer_radar_sr73f/lib/dozer_radar_sr73f/radar_sr73f_node") module=radar ;;
        "$workspace/install/dozer_audio/lib/dozer_audio/audio_capture_node") module=audio ;;
        "$workspace/install/dozer_perception/lib/dozer_perception/yolo_node.py") module=perception ;;
        "$workspace/install/dozer_fusion/lib/dozer_fusion/fusion_node.py") module=fusion ;;
        "$workspace/install/dozer_rtc_edge/lib/dozer_rtc_edge/rtc_edge") module=rtc ;;
        "$workspace/install/dozer_perception/lib/dozer_perception/detection_viewer.py") module=viewer ;;
        /opt/ros/humble/lib/tf2_ros/static_transform_publisher)
          parent_id=''
          while read -r field value remainder; do
            if [[ "$field" == PPid: ]]; then
              parent_id="$value"
              break
            fi
          done < "$process/status" 2>/dev/null || continue
          if [[ -n "$parent_id" && "$(readlink "/proc/$parent_id/cwd" 2>/dev/null)" == "$workspace" ]]; then
            parent_args=()
            mapfile -d '' -t parent_args < "/proc/$parent_id/cmdline" 2>/dev/null || continue
            if [[ " ${parent_args[*]} " == *' launch dozer_bringup '* ]]; then
              module=tf
            fi
          fi
          ;;
      esac
    done
    if [[ -n "$module" ]]; then
      process_id="${process##*/}"
      running["$module"]+="$process_id "
    fi
  done
}

prepare_launch() {
  find_running_modules
  local module enabled launch_argument expected_count
  local -a process_ids
  launch_command=(ros2 launch dozer_bringup dozer.launch.py)
  for module in "${modules[@]}"; do
    enabled=true
    if [[ "$module" == viewer && "$viewer" == false ]]; then
      launch_command+=(use_viewer:=false)
      continue
    fi
    if [[ "$existing" == keep && -n "${running[$module]:-}" ]]; then
      expected_count=1
      case "$module" in
        camera) expected_count=6 ;;
        perception) expected_count=2 ;;
      esac
      read -r -a process_ids <<< "${running[$module]}"
      if (( ${#process_ids[@]} < expected_count )); then
        printf '%s is only partially running (%s/%s processes); use --restart to recover the full module.\n' \
          "$module" "${#process_ids[@]}" "$expected_count" >&2
        return 1
      fi
      enabled=false
      printf 'Keeping %s (PID: %s)\n' "$module" "${running[$module]% }"
    elif [[ -n "${running[$module]:-}" ]]; then
      printf 'Restarting %s (PID: %s)\n' "$module" "${running[$module]% }"
    else
      printf 'Starting %s\n' "$module"
    fi
    case "$module" in
      tf) launch_argument=use_static_tf ;;
      rtc) launch_argument=use_rtc_edge ;;
      *) launch_argument="use_$module" ;;
    esac
    launch_command+=("$launch_argument:=$enabled")
  done
}

stop_running_modules() {
  local module process_id
  local -a process_ids=()
  for module in "${modules[@]}"; do
    for process_id in ${running[$module]:-}; do
      process_ids+=("$process_id")
      if kill -0 "$process_id" 2>/dev/null; then
        kill -INT "$process_id" 2>/dev/null || {
          printf 'Cannot stop %s (PID: %s); refusing to start duplicates.\n' "$module" "$process_id" >&2
          return 1
        }
      fi
    done
  done
  for process_id in "${process_ids[@]}"; do
    if ! timeout 15 tail --pid="$process_id" -f /dev/null; then
      printf 'PID %s did not stop within 15 seconds; no new modules started.\n' "$process_id" >&2
      return 1
    fi
  done
  find_running_modules
  if (( ${#running[@]} > 0 )); then
    printf 'Workspace modules are still running or were respawned; no new modules started.\n' >&2
    return 1
  fi
}

build_command=(colcon build --symlink-install --packages-up-to
  dozer_audio dozer_camera_gmsl dozer_radar_sr73f dozer_perception
  dozer_fusion dozer_bringup dozer_rtc_edge
  --cmake-args -DCMAKE_BUILD_TYPE=Release)

if [[ "$dry_run" == true ]]; then
  printf 'Workspace: %s\n' "$workspace"
  printf 'Existing modules: %s\n' "$existing"
  prepare_launch
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

prepare_launch
if [[ "$existing" == restart ]]; then
  stop_running_modules
fi
if [[ " ${launch_command[*]} " != *':=true '* ]]; then
  printf 'All requested modules are already running; nothing to start.\n'
  exit 0
fi
printf 'Starting missing modules (existing: %s, preview: %s). Press Ctrl+C to stop newly launched modules.\n' "$existing" "$viewer"
exec "${launch_command[@]}"