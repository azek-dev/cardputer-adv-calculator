# Installation Guide

How to flash this calculator firmware onto a Cardputer ADV / Cardputer
without setting up a development environment.

## Method 1: Flash from your browser (recommended)

1. Open the **[install page](https://azek-dev.github.io/cardputer-adv-calculator/)**
   in **Chrome** or **Edge** (requires the Web Serial API — Safari and
   Firefox are not supported)
2. Connect the Cardputer ADV to your computer with a USB-C cable
3. Click the button on the page, pick the device's port from the list, then
   click `INSTALL`
4. The device reboots automatically once flashing finishes, showing the
   calculator screen

Saved history, Wi-Fi credentials, and the clock setting are preserved across
future updates done this way.

## Method 2: Manual flashing with esptool.py

A fallback for environments where Web Serial isn't available (e.g.
Firefox). Requires Python.

1. Download these four files into the same folder:
   - [bootloader.bin](docs/firmware/bootloader.bin)
   - [partitions.bin](docs/firmware/partitions.bin)
   - [boot_app0.bin](docs/firmware/boot_app0.bin)
   - [firmware.bin](docs/firmware/firmware.bin)
2. Install esptool:
   ```bash
   pip install esptool
   ```
3. Connect the Cardputer ADV over USB-C and find its serial port name:
   - Mac: `ls /dev/cu.usbmodem*`
   - Windows: check `COM*` in Device Manager
4. Run (replacing `<PORT>` with your device's port):
   ```bash
   python -m esptool --chip esp32s3 --port <PORT> --baud 460800 \
     --before default_reset --after hard_reset write_flash -z \
     --flash_mode dio --flash_freq 80m --flash_size 8MB \
     0x0000 bootloader.bin \
     0x8000 partitions.bin \
     0xe000 boot_app0.bin \
     0x10000 firmware.bin
   ```

## After flashing

Type a math expression on the keyboard (e.g. `2*sin(pi/4)+sqrt(16)`) and
press Enter to evaluate it. See [MANUAL.md](MANUAL.md) for the full guide.

## Building from source

If you'd rather set up a development environment, see the "Building"
section in [README.md](README.md) (requires
[PlatformIO Core](https://platformio.org/install/cli)).
