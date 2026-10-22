#!/usr/bin/env bash
# Read the documented J2 Samsung TSP SYNC register. Only writes the exact
# volatile F0 unlock/relock sequence from the matched vendor panel DTS.
# No B9 data write, display-mode switch, flash, or calibration operation.
set -euo pipefail
serial=${1:-f40df34d}
adb -s "$serial" shell '
set -e
base=/sys/class/drm/card0-DSI-1
info=$(cat "$base/panel_info")
case "$info" in
  panel_name=mdss_dsi_j2_p2_1_38_0c_0a_dsc_cmd) ;;
  *) echo "Unexpected panel: $info" >&2; exit 1 ;;
esac
echo "$info"
trap '\''echo "0 0 39 01 00 00 00 00 03 F0 A5 A5" > "$base/mipi_reg"'\'' EXIT
echo "0 0 39 01 00 00 00 00 03 F0 5A 5A" > "$base/mipi_reg"
echo "1 9 06 01 00 01 00 00 01 B9" > "$base/mipi_reg"
cat "$base/mipi_reg"
'
