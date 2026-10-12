#!/usr/bin/env bash
# 录制车端数据（MCAP 格式）。默认不录原始图像，避免占满磁盘。
# 用法：ros2 run dozer_tools record_bag.sh [输出目录]
set -euo pipefail
out_dir="${1:-$HOME/bags/dozer_$(date +%Y%m%d_%H%M%S)}"
exec ros2 bag record -s mcap -o "${out_dir}" \
  --regex '^/(radar|perception|fusion|vehicle)/.*|^/tf$|^/tf_static$|^/diagnostics$'
