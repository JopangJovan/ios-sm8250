#!/bin/bash
# Build a conservative Xiaomi Mi 10 (umi) boot image around an existing
# kernel.development.umi. The iOS root already present in userdata is not
# modified. The board-specific power/volume key driver is enabled; touch and
# USB stay disabled until their SM8250 ports are independently verified.
set -euo pipefail

repo=$(cd "$(dirname "$0")/.." && pwd)
kernel=${1:-"$repo/xnu/out/kernel.development.umi"}
dtb=${2:-"$repo/dtb/umi.dtb"}
out=${3:-"$repo/shim/out/shim-boot-umi-safe.img"}

[ -f "$kernel" ] || { echo "error: missing UMI kernel: $kernel" >&2; exit 1; }
[ -f "$dtb" ] || { echo "error: missing UMI DTB: $dtb" >&2; exit 1; }
kernel=$(realpath "$kernel")
dtb=$(realpath "$dtb")

mkdir -p "$repo/xnu/out" "$repo/shim/out"
adt="$repo/shim/out/umi-safe.adt"
payload="$repo/shim/out/umi-safe-payload.bin"

python3 "$repo/tools/mkadt.py" "$adt" --board umi --highmem
python3 "$repo/tools/mkpayload.py" "$kernel" "$adt" "$payload" \
    --cmdline 'debug=0xc serial=3 cpus=8 keepsyms=1 -enable_kprintf_spam -noprogress rd=ufsroot0 unentitled_ios_sim_launch=1 ignition_level=1 wdt_test=no_user_checkin ufsrw=1 rootrw=1 launchd_unsecure_cache=1 wdt=-1 sweetfb_noconsole=1 sweetfb_hz=30 sweettouch=0 sweetkeys=1 sweetusb=0'

(cd "$repo" && BOARD=umi DTB="$dtb" shim/build.sh ../shim/out/umi-safe-payload.bin)
cp "$repo/shim/out/shim-boot.img" "$out"
sha256sum "$out"
