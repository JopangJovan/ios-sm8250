# ios-sm8250

**Apple's iOS 16 running on a Redmi Note 10 Pro and a Xiaomi Mi 10.**

This project ports Apple's open-source XNU kernel (xnu-8796, the iOS 16 generation) to Qualcomm SM7150 and SM8250 phones. On top of it runs the **unmodified iOS 16.7.16 (20H392) userland of the iPhone X**: launchd, the system daemons, backboardd, SpringBoard and the stock apps.

| Board | Phone | SoC | State |
| --- | --- | --- | --- |
| `sweet` | Redmi Note 10 Pro | SM7150 / Snapdragon 732G | Boots to the iOS home screen; touch, buttons and display work |
| `umi` | Xiaomi Mi 10 | SM8250 / Snapdragon 865 | Bring-up port: kernel patch, Apple device tree and shim build; **not yet tested on hardware** |

The `sweet` result below is from **2026-10-18**: the phone boots to the **iOS home screen**. You can unlock it, open Settings, pull down Control Center and the notification center, and use the touchscreen and the hardware buttons. Rendering runs entirely on the CPU; there is no GPU driver. The Mi 10 port uses the same kernel and userland but has not booted on a phone yet.

> Experimental bring-up, not a usable phone OS. Many things are missing or stubbed (see below), and Apple's security model is mostly switched off. It installs into the `userdata` partition and so **erases Android's user data**.

## What works

The table describes the tested `sweet` port. `umi` reuses the same drivers with SM8250 board values; treat every `umi` item as unverified until a boot log says otherwise.

| Area | State |
| --- | --- |
| Boot | Stock Xiaomi bootloader → `fastboot boot` of our shim → XNU. Nothing is flashed to the boot partitions |
| CPU | 6 of the 8 cores (the six Cortex-A55s) via PSCI, 16K pages, GICv3 interrupts, timers |
| Memory | About 3.95 GiB of the upper DDR bank |
| Storage | Own UFS host driver; the iOS root is a journaled HFS+ volume in `userdata`, read/write |
| iOS userland | Real iPhone X 16.7.16 root filesystem and shared cache; launchd and the system daemons run |
| Display | Kernel IOSurface + IOMobileFramebuffer (`AppleCLCD`) services; CoreAnimation renders in software; frames are copied into the bootloader's framebuffer. Presented to iOS as an iPhone X panel (1125×2436) |
| GUI | backboardd, SpringBoard, lock screen, home screen, Control Center, notification center, Settings |
| Touch | Goodix GT9896 over I²C (GENI), rail enabled through RPMh, delivered as HID digitizer events |
| Buttons | Power, volume up/down (PMIC through SPMI) as HID consumer keys; lock/wake works |
| Battery | IOPMPowerSource service (static 100 %, charging icon; real fuel-gauge readout prepared) |
| Device identity | iPhone X (`iPhone10,3` / `D22AP`): no Home button, gesture UI |
| Activation | Native "Hactivation" path of mobileactivationd, chosen through device-tree security status. No Apple servers involved |

## What does not work (yet)

- **Navigation:** you cannot leave an app (the home gesture and AssistiveTouch Home do nothing yet).
- **No Wi-Fi, Bluetooth, cellular, audio, camera, sensors, GPS or SEP / Face ID.** Daemons for them start and fail or keep relaunching.
- **No GPU.** Everything is drawn by the CPU at roughly 20–30 fps. The two Cortex-A76 cores are not used yet.
- **Few apps.** The image has only what is on the system volume. There is no App Store, no Apple ID, and some Settings pages are empty.
- **Slow boot:** about 4 minutes on the Apple logo.
- **Security is off:** permissive code signing (AMFI) and sandbox policies, a stub AppleKeyStore, no data protection. Do not put anything personal on it.

## Mi 10 (`umi`) status

The Mi 10 port is a **separate XNU machine config**, `UMI`, in patch `0082`. It keeps the `sweet` implementation and adds SM8250 values instead of forking drivers:

| Item | `sweet` (SM7150) | `umi` (SM8250) |
| --- | --- | --- |
| SoC / CPU | Kryo 470: 6× A55 + 2× A76 | Kryo 585: 4× A55 + 3× A77 + 1× A77 prime, MPIDR `0x0`–`0x700` |
| Timer PPI | Qualcomm numbering, virtual timer INTID 19 | ARM numbering, virtual timer INTID 27 |
| Panel / splash | 1080×2400 at `0x9c000000`, region `0x1700000` | 1080×2340 at `0x9c000000`, region `0x2300000` |
| Touch | Goodix GT9896 or FocalTech at SE7 (`0xa84000`) | FocalTech FTS at `0x49`, SE13 (`0xa94000`), TLMM `0x0f000000` |
| Touch GPIOs | SDA/SCL 6/7, reset 8, IRQ 9, power 90 | SDA/SCL 36/37, reset 38, IRQ 39, power 69 |
| Buttons | PON SID 0; volume-up PM6150L SID 4 GPIO2 | PON SID 0; volume-up PM8150 SID 0 GPIO6 |
| UFS | `0x1d84000`, polled | same host, polled |
| RAM for XNU | upper bank `0x100000000` + `0xfcc00000` | same upper bank; valid for 6/8/12 GB variants |

iOS is still presented as an iPhone X (`1125×2436` logical). The native 1080×2340 panel is centred inside that canvas.

**Not tested yet.** There is no Mi 10 boot log, CI kernel, or flashable image in this repository. The first hardware test should use `cpus=1` and watch for the early console, then try the touch/keys drivers. The Goodix GT9889 at `0x5d` on the same SE13 bus is a follow-up; the primary path is FocalTech. USB live logging uses SM8250's different GCC GDSC/clock offsets in patch `0082`.

## How it boots

```
ABL (stock Xiaomi bootloader, unlocked)
  -> fastboot boot shim-boot.img              (temporary; nothing is flashed to boot)
     -> shim: own MMU setup, reads the live FDT, loads the payload from the ramdisk
        -> XNU Mach-O + generated Apple Device Tree (ADT) + boot-args
           -> XNU -> UFS driver -> HFS+ root in the userdata partition
              -> dyld + shared cache -> launchd -> daemons -> backboardd + SpringBoard
```

There is no serial port (the debug UART is wired as SPI on this board). The kernel draws its console into the framebuffer that ABL leaves at `0x9c000000`, and keeps a persistent 4 MiB log ring at the end of `userdata`. You read that log from OrangeFox recovery with `tools/sweet-log-pull.sh`, so you don't have to film the screen.

## How to do it yourself

You need a phone with an **unlocked bootloader**, a Linux PC, and a GitHub account for the kernel build: a Redmi Note 10 Pro (`sweet`, SM7150) or a Xiaomi Mi 10 (`umi`, SM8250). The `sweet` path is the exact one used for the screenshots above. The `umi` path builds the same kernel and userland but has not been tested on hardware. Other variants or firmware may need changes. **Everything on `userdata` is lost.** Bootloaders, recovery, modem and persist partitions are never touched.

### 0. Back up the phone and get the DTB

Boot OrangeFox (or any recovery with adb root) temporarily and pull at least `persist`, `modemst1`, `modemst2`, `fsg`, `fsc` and `boot`, `dtbo`, `vbmeta`. They contain per-device calibration data. Keep them off git; `.gitignore` already excludes `backup/`, `dtb/` and `recovery/`.

The shim needs the **final live device tree** (with the bootloader's overlays applied), not just a base DTB. On `sweet`, unpack the stock `boot.img`:

```sh
python3 tools/unpack_bootv2.py backup/boot.img dtb/boot_unpacked   # -> dtb/boot_unpacked/dtb
```

On `umi`, the simplest source is the live FDT from rooted Android, or a DTB already dumped from the phone:

```sh
adb shell su -c 'cat /sys/firmware/fdt' > dtb/umi.dtb
# or copy an existing dump:
# cp /path/to/umi.dtb dtb/umi.dtb
```

A suitable Mi 10 DTB is 938369 bytes, has `model = "Qualcomm Technologies, Inc. xiaomi umi"`, a `/soc/ufshc@1d84000` node, an SE13 `fts@49`, and a panel/touch range of 1080×2340. `dtb/` is ignored by git because the live tree can contain the serial number.

### 1. Build the kernel

The kernel builds on GitHub's macOS runners with [blacktop/darwin-xnu-build](https://github.com/blacktop/darwin-xnu-build). The workflow fetches xnu-8796, applies `xnu/patches/` in order, and also stages the pinned open-source libpthread, HFS and BearSSL sources it needs. Fork the repo (public repos get free macOS minutes) and run:

```sh
# sweet
gh workflow run xnu.yml -R YOU/ios-sm8250 -f machine_config=SWEET
gh run download RUN_ID -R YOU/ios-sm8250 -n xnu-kernel -D xnu/out

# umi
gh workflow run xnu.yml -R YOU/ios-sm8250 -f machine_config=UMI
gh run download RUN_ID -R YOU/ios-sm8250 -n xnu-kernel -D xnu/out
```

The kernel artifacts are `kernel.development.sweet` and `kernel.development.umi`. The workflow also accepts a push to `main`; that build uses the default `SWEET` value, so dispatch `UMI` explicitly. To check a patch locally before spending a CI run, `tools/xnu-cc.py` replays the compile commands of a previous CI log for single files.

### 2. Build the iOS root image

Download the iPhone X (iPhone10,3) iOS 16.7.16 (20H392) IPSW from Apple. Then, on Linux, with `apfs-fuse`, `hfsprogs` and sudo:

```sh
# OS, SystemOS and AppOS disk images from the IPSW (see BuildManifest.plist for their names)
tools/mkiosroot.sh OS.dmg SystemOS.dmg AppOS.dmg ios16-root.img 16G
sudo fsck.hfsplus -fy ios16-root.img
sudo mount -t hfsplus -o loop,force ios16-root.img /mnt/iosroot
sudo python3 tools/sweet-root-tweaks.py /mnt/iosroot
sudo umount /mnt/iosroot
img2simg ios16-root.img ios16-root.simg
```

`mkiosroot.sh` copies the OS volume and both cryptexes into one case-sensitive journaled HFS+ volume. It adds the dyld cache links, the plain system keybag and the log directories the daemons expect. `sweet-root-tweaks.py` applies the bring-up changes: it skips Setup, seeds the activation state, removes the baseband daemon, disables the watchdogs that kill slow apps and the crash-looping media paths, and so on. The script's header explains each change.

The device tree builder copies `/product` from the real iPhone X device tree, which MobileGestalt needs. Extract it once, without downloading the whole IPSW:

```sh
python3 tools/remotezip.py IPSW_URL DeviceTree.d22ap.im4p research/ipsw-x/dt
ipsw img4 im4p extract research/ipsw-x/dt/Firmware/all_flash/DeviceTree.d22ap.im4p -o research/ipsw-x/d22ap.adt
```

### 3. Build the boot image

Needs `aarch64-linux-gnu-gcc` and `mkbootimg`.

`sweet`:

```sh
python3 tools/mkadt.py shim/out/sweet.adt --highmem
python3 tools/mkpayload.py xnu/out/kernel.development.sweet shim/out/sweet.adt shim/out/payload.bin \
    --cmdline 'debug=0xc serial=3 cpus=6 keepsyms=1 -enable_kprintf_spam -noprogress rd=ufsroot0 unentitled_ios_sim_launch=1 ignition_level=1 wdt_test=no_user_checkin ufsrw=1 rootrw=1 launchd_unsecure_cache=1 wdt=-1 sweetfb_noconsole=1 sweetfb_hz=30'
shim/build.sh ../shim/out/payload.bin      # -> shim/out/shim-boot.img
```

`umi` (note `--board umi` and `--highmem`; SM8250 low memory has a hole that XNU cannot manage, so `mkadt.py` refuses low-memory UMI):

```sh
python3 tools/mkadt.py shim/out/umi.adt --board umi --highmem
python3 tools/mkpayload.py xnu/out/kernel.development.umi shim/out/umi.adt shim/out/umi-payload.bin \
    --cmdline 'debug=0xc serial=3 cpus=1 keepsyms=1 -enable_kprintf_spam -noprogress rd=ufsroot0 unentitled_ios_sim_launch=1 ignition_level=1 wdt_test=no_user_checkin ufsrw=1 rootrw=1 launchd_unsecure_cache=1 wdt=-1 sweetfb_noconsole=1 sweetfb_hz=30'
BOARD=umi DTB=dtb/umi.dtb shim/build.sh ../shim/out/umi-payload.bin   # -> shim/out/shim-boot.img
```

For the safest first boot, use the wrapper below. It starts one CPU and disables
the still-experimental UMI touch, key and USB drivers while leaving the display,
UFS root and iOS userland enabled:

```sh
tools/build-umi-boot.sh xnu/out/kernel.development.umi dtb/umi.dtb
fastboot boot shim/out/shim-boot-umi-safe.img
```

The UMI command starts with one CPU and the native 1080×2340 framebuffer. Once the early console and touch are confirmed, increase to `cpus=4` (A55) or `cpus=8` (all cores).

What the less obvious boot-args do:

| Boot-arg | Effect |
| --- | --- |
| `rd=ufsroot0 ufsrw=1 rootrw=1` | Root on the `userdata` partition, writable |
| `cpus=6` | Start the six A55 cores; `cpus=1` is the safe fallback (`sweet`) |
| `launchd_unsecure_cache=1` | launchd accepts its job cache without AMFI |
| `wdt=-1 wdt_test=no_user_checkin` | No hardware watchdog |
| `sweetfb_noconsole=1` | Stop drawing kernel text over the GUI. It still goes to the persistent log; panics are drawn |
| `sweetfb_hz=30` | Panel refresh rate of the CPU copy |
| `sweettouch=0`, `sweetkeys=0` | Disable the touch or button driver |

### 4. Flash the root and boot

```sh
fastboot -S 512M flash userdata ios16-root.simg   # ~7 minutes; do not unplug
# Xiaomi's fastboot often hangs after a large flash: re-enter fastboot with Power + Volume down
fastboot boot shim/out/shim-boot.img
```

The first boot takes about 4–5 minutes on the Apple logo, then the lock screen appears. Swipe up to unlock. To get back to fastboot, hold Power + Volume down.

### 5. Read the kernel log

```sh
fastboot boot recovery/recovery.img        # OrangeFox, temporary
tools/sweet-log-pull.sh logs/boot.txt      # 4 MiB console ring from the end of userdata
adb reboot bootloader
```

The log contains crash reports with backtraces (`CRASH name ...`), IOKit traces of backboardd and SpringBoard, and the drivers' `SWEET ...` lines. `tools/sweet-crash.py` symbolicates them against the shared cache.

### QEMU

`tools/qemu-sweet.py` boots the same kernel on QEMU `virt` with a test RAM-disk root. It is useful for kernel-only changes; see `NOTES.md`.

## Repository layout

| Path | What |
| --- | --- |
| `shim/` | Bare-metal boot shim (C + asm) packed as an Android boot.img v2 |
| `xnu/patches/` | The XNU patch series (applied in order by CI), plus staging scripts for libpthread, HFS and BearSSL |
| `.github/workflows/xnu.yml` | Kernel CI build |
| `tools/mkadt.py` | Generates the Apple Device Tree from the board's MMIO layout (`--board sweet|umi`) and the iPhone X identity |
| `tools/mkpayload.py` | Packs kernel + ADT + boot-args into the shim payload |
| `tools/mkiosroot.sh` | Builds the HFS+ iOS root from the IPSW images |
| `tools/sweet-root-tweaks.py` | Bring-up changes to the root (Setup skip, activation, daemons) |
| `tools/sweet-log-pull.sh`, `sweet-log.py` | Read the persistent kernel log from recovery |
| `tools/sweet-crash.py` | Symbolicate console crash reports |
| `tools/xnu-cc.py` | Local single-file compile check from a CI log |
| `tools/qemu-sweet.py` | QEMU test boots with framebuffer text decoding |
| `tools/adt.py`, `remotezip.py`, `unpack_bootv2.py`, `sparsediff.py` | Helpers |
| `NOTES.md` | Detailed engineering log: every boot, crash and decision, plus hardware facts |

Not in git: partition backups, live device trees, recovery images, Apple firmware and root images, and downloaded or generated build trees. They contain device serials and calibration data, or Apple's copyrighted files.

## Kernel patches

| # | What |
| --- | --- |
| 0001–0009 | Toolchain compatibility, SWEET machine config, non-PAC arm64, stubs for Apple-internal pieces |
| 0010–0018 | Early framebuffer console, physmap fix, PRNG without corecrypto, device-tree platform expert, timer PPI, boot CPU driver |
| 0019–0020 | Apple open-source libpthread kernel provider; BearSSL AES |
| 0021–0027 | RAM-disk root, mockfs, SHA-256, minimal AMFI, Apple open-source HFS+, MD5 |
| 0028–0034 | UFS host driver and root device, read/write root, DMA above 4 GiB |
| 0035–0045 | Entitlements, bindfs, AppleKeyStore stub, IOWatchdog, console crash reports |
| 0046 | IOSurface (IOCoreSurfaceRoot) and IOMobileFramebuffer (AppleCLCD) services |
| 0047–0055 | Diagnostics: persistent log, /dev/null tee, IOKit trace, heap strings and user stack sampler on crash; permissive Sandbox policy; HFS rename-swap; AppleKeyStore selectors |
| 0056–0057 | Throttled panel copy; no external identity resolver |
| 0058 | HID event services. Apple's IOHIDFamily-1915.140.3 user client and event queue, matching the iOS 16.7 userland |
| 0059 | iPhone X identity (`hw.product` / `hw.target` from the device tree) |
| 0060 | Power and volume keys over SPMI; touch over GENI I²C |
| 0061 | Present the display as 1125×2436 |
| 0062 | RPMh regulator vote for the touch rail; battery power source |
| 0063 | GENI FIFO packing fix and the Goodix GT9896 protocol |
| 0064 | Grant PRIV_WORK_INTERVAL (UIKit's CoreAnimation work interval) |
| 0065 | SMP: secondary A55 cores via PSCI CPU_ON |
| 0066–0067 | Fault-region crash info; console mute |
| 0068 | IOSurface lookup semantics for already-mapped surfaces |
| 0069 | Batched persistent-log flushes |
| 0070–0081 | Navigation/syscall fixes, all-core SMP, display power, USB Ethernet and live log |
| 0082 | UMI machine config (Xiaomi Mi 10, SM8250): timer INTID 27, 1080×2340 panel, SE13 FocalTech touch, PM8150 keys, SM8250 USB GCC offsets, board-aware ADT/shim |

## Key hardware facts

- RAM: payloads use the upper bank, `0x100000000` + `0xfcc00000` (about 3.95 GiB), above Qualcomm's protected carve-outs. This works for both boards; `umi` must use `--highmem`.
- GICv3 with security extensions: the timer goes through Group 1 IRQ, not FIQ as on Apple silicon. `sweet` numbers the virtual timer PPI as INTID 19; `umi` uses the architected INTID 27.
- PSCI through `smc`, 19.2 MHz timer, ARMv8.2 without pointer authentication. MPIDR uses Aff1 for the core number.
- `sweet` panel: 1080×2400 DSC video mode, continuous splash from ABL, no refresh kick needed.
- `sweet` touch: Goodix GT9896 at I²C `0x5d` on QUP SE7 (`0xa84000`), powered by PM6150L LDO11 through RPMh.
- `sweet` buttons: power and volume down on the PM6150 PON block, volume up on PM6150L GPIO2 (SPMI SID 4).
- `umi` panel: 1080×2340, continuous splash at `0x9c000000` (region `0x2300000`), still presented as iPhone X 1125×2436.
- `umi` touch: FocalTech FTS at I²C `0x49` on QUPv3 SE13 (`0xa94000`), TLMM `0x0f000000`, GPIOs 36/37/38/39, 3.3 V enable 69. Goodix GT9889 at `0x5d` on the same bus is a follow-up.
- `umi` buttons: power/volume-down on PM8150 PON SID 0 (PPID `0x008`), volume-up on PM8150 GPIO6 (PPID `0x0c5`).
- `umi` CPU: Kryo 585, MPIDRs `0x0`–`0x700`; the ADT exports all eight as one cluster. Start with `cpus=1`, then `cpus=4` (A55) or `cpus=8`.

More in [`NOTES.md`](NOTES.md).

## Credits

Built on Apple's open-source releases (xnu, IOHIDFamily, libpthread, hfs), [BearSSL](https://bearssl.org), [blacktop/darwin-xnu-build](https://github.com/blacktop/darwin-xnu-build), [ipsw](https://github.com/blacktop/ipsw), Qualcomm/Xiaomi's published Linux kernel sources for the hardware details, and [OrangeFox Recovery](https://orangefox.download). iOS itself is Apple's and is not distributed here.
