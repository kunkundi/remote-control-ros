#!/usr/bin/env bash
set -eo pipefail

workspace="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
setup="$workspace/install/setup.bash"
console="$workspace/install/dozer_rtc_console/lib/dozer_rtc_console/rtc_console"

if [[ ! -r "$setup" || ! -x "$console" ]]; then
  (
    cd "$workspace"
    source /opt/ros/humble/setup.bash
    MAKEFLAGS="${MAKEFLAGS:--j1}" CMAKE_BUILD_PARALLEL_LEVEL="${CMAKE_BUILD_PARALLEL_LEVEL:-1}" \
      colcon build --executor sequential --symlink-install --packages-up-to dozer_rtc_console \
        --cmake-args -DCMAKE_BUILD_TYPE=Release
  )
fi

source "$setup"
exec ros2 run dozer_rtc_console rtc_console "$@"
