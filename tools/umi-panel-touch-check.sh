#!/usr/bin/env bash
# Temporary DCS display-off/on check; no sleep, reset or vendor register writes.
set -euo pipefail
serial=${1:-f40df34d}
adb -s "$serial" shell '
set -e
base=/sys/class/drm/card0-DSI-1
case "$(cat "$base/panel_info")" in
  panel_name=mdss_dsi_j2_p2_1_38_0c_0a_dsc_cmd) ;;
  *) echo "Unexpected panel" >&2; exit 1 ;;
esac
test -x /tmp/umi-fts-snapshot
trap '\''echo "0 0 05 01 00 00 64 00 01 29" > "$base/mipi_reg"'\'' EXIT
echo "DISPLAY OFF"
echo "0 0 05 01 00 00 64 00 01 28" > "$base/mipi_reg"
/tmp/umi-fts-snapshot
echo "DISPLAY ON"
echo "0 0 05 01 00 00 64 00 01 29" > "$base/mipi_reg"
/tmp/umi-fts-snapshot
'
