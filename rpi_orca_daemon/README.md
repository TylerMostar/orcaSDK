# ORCA Raspberry Pi Haptics Daemon

This app keeps one or more ORCA actuators in **Haptic mode** with **damping = 600**.
It is intended to run persistently on a Raspberry Pi 5.

## What it does
- Connects to each serial device you provide (recommended: stable udev names like `/dev/rs232_to_usb_converter_1`)
- Configures each actuator with:
  - `set_damper(600)`
  - `enable_haptic_effects(Damper)`
  - `set_mode(HapticMode)`
  - streaming enabled
- Runs continuously and auto-reconnects if communication fails

## Build on Raspberry Pi
From the repo root (`orcaSDK`):

```bash
cmake -S rpi_orca_daemon -B build/rpi_orca_daemon -DCMAKE_BUILD_TYPE=Release
cmake --build build/rpi_orca_daemon -j
```

Binary output:

```bash
build/rpi_orca_daemon/orca_haptics_daemon
```

## Run manually

```bash
./build/rpi_orca_daemon/orca_haptics_daemon --device /dev/rs232_to_usb_converter_1 --device /dev/rs232_to_usb_converter_2 --damping 600
```

For 4 motors (one converter per motor), pass all 4 devices:

```bash
./build/rpi_orca_daemon/orca_haptics_daemon --device /dev/rs232_to_usb_converter_1 --device /dev/rs232_to_usb_converter_2 --device /dev/rs232_to_usb_converter_3 --device /dev/rs232_to_usb_converter_4 --damping 600
```

The daemon starts one worker per `--device`/`--device-address` and prints per-device `[STATUS]` lines (`WAITING_FOR_SERIAL`, `WAITING_FOR_HAPTICS`, `HAPTICS_ACTIVE`).

### Optional arguments
- `--baud <int>` (default `19200`)
- `--interframe-us <int>` (default `2000`)
- `--address <int>` global Modbus address for all devices (default `1`)
- `--device-address /dev/orca_name:addr` set per-device address
- `--open-retry-ms <int>` retry interval when port is not open (default `200`)
- `--configure-retry-ms <int>` retry interval for haptics configuration while waiting for motor electronics (default `100`)
- `--health-check-ms <int>` mode/communication health check interval after configured (default `500`)
- `--serial-log-dir <path>` optional raw Modbus tx/rx logs for debugging communication

### Fast-engagement profile (recommended for your power-up use case)

```bash
./build/rpi_orca_daemon/orca_haptics_daemon --device /dev/rs232_to_usb_converter_1 --damping 600 --baud 19200 --interframe-us 2000 --open-retry-ms 50 --configure-retry-ms 20 --health-check-ms 100
```

For raw serial diagnostics, create a writable log directory and add `--serial-log-dir`:

```bash
mkdir -p /tmp/orca-serial
./build/rpi_orca_daemon/orca_haptics_daemon --device /dev/rs232_to_usb_converter_1 --damping 600 --serial-log-dir /tmp/orca-serial
```

## Run persistently with systemd
A service file template is provided at:

- `rpi_orca_daemon/systemd/orca-haptics-daemon.service`

Update `User`, `WorkingDirectory`, and `ExecStart` for your Pi deployment path.

Example install commands:

```bash
sudo cp rpi_orca_daemon/systemd/orca-haptics-daemon.service /etc/systemd/system/
sudo systemctl daemon-reload
sudo systemctl enable orca-haptics-daemon
sudo systemctl start orca-haptics-daemon
sudo systemctl status orca-haptics-daemon
```

For 4 motors, ensure your service `ExecStart` includes all 4 devices (or 4 `--device-address` entries).

View runtime logs:

```bash
journalctl -u orca-haptics-daemon -f
```

## Device permissions and latency
On Linux, ensure each serial device has appropriate permissions and low latency settings as described in the repo `README.md`.
If you already use udev rules to create stable symlinks, always pass those names to this daemon instead of `/dev/ttyUSB*`.

## Quick connection checks
On the Pi, verify the service is using the device path you expect:

```bash
sudo systemctl cat orca-haptics-daemon
ls -l /dev/rs232_to_usb_converter_1
readlink -f /dev/rs232_to_usb_converter_1
```

Then confirm the `pi` user can access the serial device and the low-latency setting is active:

```bash
groups pi
setserial -g /dev/rs232_to_usb_converter_1
journalctl -u orca-haptics-daemon -n 100 --no-pager
```
