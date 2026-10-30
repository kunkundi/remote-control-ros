#!/usr/bin/env bash
set -euo pipefail

workspace="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
if [[ -z "${DISPLAY:-}" && -z "${WAYLAND_DISPLAY:-}" ]]; then
  printf 'No graphical display available for the detection viewer.\n' >&2
  exit 1
fi
exec "$workspace/start_all.sh" "$@"
