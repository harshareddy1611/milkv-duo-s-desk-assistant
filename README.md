# Milk-V Duo S Desk Assistant

An on-device (no cloud) computer-vision assistant for my office desk, built on a
[Milk-V Duo S](https://milkv.io/duo-s) (Sophgo SG2000, ARM Cortex-A53 + RISC-V C906, ~1 TOPS NPU).

## Planned features

1. **Presence-aware auto-lock + break reminder** — detect when I'm away from the desk and
   lock the PC / cut monitor power; track continuous sitting time and nudge a break.
2. **Gesture shortcuts** — hand-raise/swipe detection to mute mic or pause music.
3. **Office status light** — LED strip via relay showing presence + calendar-busy state.

Detection runs on the SG2000's onboard NPU, so no video ever leaves the device.

## Hardware

- Milk-V Duo S (SD-card variant, no onboard eMMC), boot switch set to **ARM**
- Raspberry Pi Camera Module 2 (Sony IMX219), connected to the **J2** (RPi-style) CSI connector
- CP2102 USB-to-TTL adapter for the debug serial console (3.3V logic)
- Basic relays/sensors (repurposed from other projects) for the status light / lock triggers

## SDK build notes

The stock `duo-buildroot-sdk-v2` doesn't support Camera Module 2 (IMX219) or Module 3
(IMX708) out of the box — only **CAM-GC2083** (J1) and **OV5647** (J2) are officially
supported. IMX219 support comes from an unmerged community patch:
[milkv-duo/duo-buildroot-sdk-v2#73](https://github.com/milkv-duo/duo-buildroot-sdk-v2/pull/73)
by [@bleemesser](https://github.com/bleemesser), applied here on top of the `develop`
branch. A copy of the patch is kept in [`patches/`](patches/) for reproducibility.

To rebuild:

```bash
git clone -b develop https://github.com/milkv-duo/duo-buildroot-sdk-v2.git
cd duo-buildroot-sdk-v2
git apply /path/to/patches/0001-imx219-sensor-driver-duo-s.patch
wget https://github.com/milkv-duo/duo-buildroot-sdk-v2/releases/download/dl/dl.tar
tar xf dl.tar -C ./buildroot/
FORCE_UNSAFE_CONFIGURE=1 ./build.sh milkv-duos-glibc-arm64-sd
```

Notes from getting this working on Ubuntu/WSL:
- Ubuntu 24.04+ removes `python3-distutils` and `libncurses5` — use **Ubuntu 22.04** instead.
- If building inside WSL, disable Windows PATH interop (`[interop]\nappendWindowsPath = false`
  in `/etc/wsl.conf`, then `wsl --terminate <distro>`) — otherwise Buildroot's dependency
  check fails on spaces in `C:\Program Files\...` entries leaking into `$PATH`.
- If your build environment's default user is `root` (e.g. a fresh WSL distro with no
  separate user created), pass `FORCE_UNSAFE_CONFIGURE=1` — Buildroot refuses to run package
  `configure` scripts as root otherwise.

After switching the camera on the board, the sensor config symlink needs to be repointed:

```bash
rm -f /mnt/data/sensor_cfg.ini
ln -s sensor_cfg_IMX219_J2.ini /mnt/data/sensor_cfg.ini   # 1080p30; use _5M_J2 for 2560x1920@27
```
