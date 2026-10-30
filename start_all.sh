#!/usr/bin/env bash
set -euo pipefail

workspace="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
build=true
dry_run=false
viewer=false
edge_only=false
existing=keep
policy_argument=''
perception_cameras=front_left,front_right
perception_argument=false
if [[ -n "${DISPLAY:-}" || -n "${WAYLAND_DISPLAY:-}" ]]; then
  viewer=true
fi

usage() {
  if [[ "$edge_only" == true ]]; then
    printf '%s\n' \
      'Usage: ./start_edge.sh [--keep|--restart] [--no-build] [--dry-run]' \
      'Start RTC edge with direct cameras and audio only.'
  else
    printf '%s\n' \
      'Usage: ./start_all.sh [--keep|--restart] [--no-build] [--headless] [--edge-only] [--dry-run]' \
      'Start direct-camera RTC edge, shared-memory perception, radar, audio, fusion and static TF.' \
      '  --headless  Disable the preview window (automatic without a display).' \
      '  --edge-only Start only direct-camera RTC edge and audio.' \
      '  --perception-cameras NAMES  Comma-separated YOLO cameras (default: front_left,front_right).' \
      '                             Choices: front_left,front_right,front,rear,left,right.'
  fi
  printf '%s\n' \
    '  --no-build  Use the existing workspace installation.' \
    '  --dry-run   Print commands without building or accessing hardware.' \
    '  --keep     Keep running modules and start missing modules (default).' \
    '  --restart  Stop existing requested modules before starting them again.' \
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
    --edge-only) edge_only=true ;;
    --dry-run) dry_run=true ;;
    --perception-cameras)
      (( $# >= 2 )) || argument_error '--perception-cameras requires a camera list.'
      perception_cameras="$2"
      perception_argument=true
      shift
      ;;
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

if [[ "$edge_only" == true && "$perception_argument" == true ]]; then
  argument_error '--perception-cameras cannot be used with --edge-only.'
fi
[[ -n "$perception_cameras" && "$perception_cameras" != *, ]] || argument_error 'Invalid perception camera list.'
IFS=, read -r -a selected_perception_cameras <<< "$perception_cameras"
declare -A selected_camera_names=()
for camera_name in "${selected_perception_cameras[@]}"; do
  case "$camera_name" in
    front_left|front_right|front|rear|left|right) ;;
    *) argument_error "Unknown perception camera: $camera_name" ;;
  esac
  [[ -z "${selected_camera_names[$camera_name]:-}" ]] || argument_error "Duplicate perception camera: $camera_name"
  selected_camera_names["$camera_name"]=true
done

modules=(camera radar audio perception fusion tf rtc viewer)
if [[ "$edge_only" == true ]]; then
  modules=(camera audio rtc)
  viewer=false
fi
declare -A running=()
declare -A running_perception=()

find_running_modules() {
  running=()
  running_perception=()
  local process process_id module argument parent_id field value remainder camera_name
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
    if [[ -n "$module" && " ${modules[*]} " == *" $module "* ]]; then
      process_id="${process##*/}"
      running["$module"]+="$process_id "
      if [[ "$module" == perception ]]; then
        camera_name=unknown
        for argument in "${command_args[@]}"; do
          if [[ "$argument" == __node:=yolo_* ]]; then
            camera_name="${argument#__node:=yolo_}"
          fi
        done
        case "$camera_name" in
          front_left|front_right|front|rear|left|right) ;;
          *) camera_name=unknown ;;
        esac
        running_perception["$camera_name"]=$(( ${running_perception[$camera_name]:-0} + 1 ))
      fi
    fi
  done
}

perception_matches_selection() {
  local camera_name
  (( ${#running_perception[@]} == ${#selected_perception_cameras[@]} )) || return 1
  for camera_name in "${selected_perception_cameras[@]}"; do
    [[ "${running_perception[$camera_name]:-0}" == 1 ]] || return 1
  done
}

prepare_launch() {
  find_running_modules
  local module enabled launch_argument expected_count camera_name
  local -a process_ids
  launch_command=(ros2 launch dozer_bringup dozer.launch.py use_camera:=false image_backend:=shm)
  launch_command+=("perception_cameras:=$perception_cameras")
  if [[ "$edge_only" == true ]]; then
    launch_command+=(use_radar:=false use_perception:=false use_fusion:=false
      use_static_tf:=false use_viewer:=false)
  fi
  for module in "${modules[@]}"; do
    enabled=true
    if [[ "$module" == camera ]]; then
      if [[ -n "${running[camera]:-}" ]]; then
        if [[ "$existing" == keep ]]; then
          printf 'Legacy ROS camera nodes are running; direct capture requires an explicit --restart.\n' >&2
          return 1
        fi
        printf 'Stopping legacy camera nodes (PID: %s); edge will own the devices.\n' "${running[camera]% }"
      fi
      continue
    fi
    if [[ "$module" == viewer && "$viewer" == false ]]; then
      launch_command+=(use_viewer:=false)
      continue
    fi
    if [[ "$existing" == keep && -n "${running[$module]:-}" ]]; then
      if [[ "$module" == rtc && ( ! -f /dev/shm/dozer_camera_front_left || ! -f /dev/shm/dozer_camera_front_right ) ]]; then
        printf 'Running edge has no shared camera output; use --restart to activate direct capture.\n' >&2
        return 1
      fi
      if [[ "$module" == rtc && "$edge_only" == false ]]; then
        for camera_name in "${selected_perception_cameras[@]}"; do
          if [[ ! -f "/dev/shm/dozer_camera_$camera_name" ]]; then
            printf 'Running edge has no shared output for %s; use --restart.\n' "$camera_name" >&2
            return 1
          fi
        done
      fi
      if [[ "$module" == perception ]] && ! perception_matches_selection; then
        printf 'Running YOLO cameras do not match %s; use --restart to change or recover the selection.\n' "$perception_cameras" >&2
        return 1
      fi
      expected_count=1
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

build_packages=(dozer_audio dozer_bringup dozer_rtc_edge)
if [[ "$edge_only" == false ]]; then
  build_packages+=(dozer_radar_sr73f dozer_perception dozer_fusion)
fi
build_command=(colcon build --symlink-install --packages-up-to "${build_packages[@]}"
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

if [[ ! -f /opt/ros/humble/setup.bash ]]; then
  printf 'ROS 2 Humble setup not found: /opt/ros/humble/setup.bash\n' >&2
  exit 1
fi
if [[ "$edge_only" == false ]]; then
  gpu_python="$workspace/.venv-gpu/bin/python"
  model="$workspace/src/perception/dozer_perception/model/best.pt"
  cudnn_lib="$workspace/.venv-gpu/lib/python3.10/site-packages/nvidia/cudnn/lib"
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
if [[ "$edge_only" == false ]]; then
  export PATH="$workspace/.venv-gpu/bin:$PATH"
fi

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