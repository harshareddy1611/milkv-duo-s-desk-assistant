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

## Presence daemon

[`board/presence_daemon.c`](board/presence_daemon.c) runs on the board and does the actual
detection. It's a stripped-down version of the SDK's `sample_vi_detection` demo (same
`tdl_sdk` camera/detection API) with the RTSP streaming removed and simple state tracking
added: it watches for `OBJECT_TYPE_PERSON` (class id 4) detections from the
`yolov8n_det_person_vehicle` model, and after 25 seconds with no person detected, writes
`away` to `/tmp/presence_state` on the board (`present` otherwise). It also flags
`/tmp/break_reminder_flag` after 50 continuous minutes of presence.

It drops into the SDK's `tdl_sdk/sample/c/camera/` directory (picked up automatically by
that folder's CMake glob) and rebuilds as part of the normal `build.sh` flow above. Run with
`PRESENCE_DEBUG=1` to log every frame's raw detection score — useful for diagnosing false
positives/negatives without needing to view the video feed.

**A frame-lifecycle bug cost a lot of debugging time here**: calling `TDL_DestroyImage()`
before `ReleaseCameraFrame()` (the reverse of the SDK samples' order) silently corrupted the
frame pool, causing the detection loop to keep reprocessing a stale/frozen frame instead of
live ones — the daemon would report a confident, consistent "person present" indefinitely
even after the person had left. Symptoms were a rock-steady detection score across hundreds
of frames instead of natural variation. Always release the camera frame before destroying
the image wrapper.

[`scripts/presence_poller.ps1`](scripts/presence_poller.ps1) runs on the PC (not the board):
it polls `/tmp/presence_state` over SSH every few seconds and locks the Windows session the
moment it sees `away`. It intentionally does not attempt to unlock on `present` — Windows
requires real authentication to unlock, and that's the point of a lock screen, not something
to bypass.
