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
./build/rpi_orca_daemon/orca_haptics_daemon --device /dev/rs232_to_usb_converter_1 --device /dev/orca_rear --damping 600
```

### Optional arguments
- `--baud <int>` (default `19200`)
- `--interframe-us <int>` (default `2000`)
- `--address <int>` global Modbus address for all devices (default `1`)
- `--device-address /dev/orca_name:addr` set per-device address
- `--open-retry-ms <int>` retry interval when port is not open (default `200`)
- `--configure-retry-ms <int>` retry interval for haptics configuration while waiting for motor electronics (default `100`)
- `--health-check-ms <int>` mode/communication health check interval after configured (default `500`)

### Fast-engagement profile (recommended for your power-up use case)

```bash
./build/rpi_orca_daemon/orca_haptics_daemon --device /dev/rs232_to_usb_converter_1 --damping 600 --baud 19200 --interframe-us 2000 --open-retry-ms 50 --configure-retry-ms 20 --health-check-ms 100
```

## Run persistently with systemd
A service file template is provided at:

- `rpi_orca_daemon/systemd/orca-haptics-daemon.service`

Update `User`, `WorkingDirectory`, and `ExecStart` for your Pi deployment path.

Example install commands:

```bash
sudo mkdir -p /opt/orca-haptics/bin
sudo cp build/rpi_orca_daemon/orca_haptics_daemon /opt/orca-haptics/bin/
sudo cp rpi_orca_daemon/systemd/orca-haptics-daemon.service /etc/systemd/system/
sudo systemctl daemon-reload
sudo systemctl enable orca-haptics-daemon
sudo systemctl start orca-haptics-daemon
sudo systemctl status orca-haptics-daemon
```

View runtime logs:

```bash
journalctl -u orca-haptics-daemon -f
```

## Device permissions and latency
On Linux, ensure each serial device has appropriate permissions and low latency settings as described in the repo `README.md`.
If you already use udev rules to create stable symlinks, always pass those names to this daemon instead of `/dev/ttyUSB*`.
