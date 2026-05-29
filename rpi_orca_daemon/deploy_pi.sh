#!/usr/bin/env bash
set -euo pipefail

BUILD_DIR="build/rpi_orca_daemon"

cmake -S rpi_orca_daemon -B "${BUILD_DIR}" -DCMAKE_BUILD_TYPE=Release
cmake --build "${BUILD_DIR}" -j

sudo install -m 0644 "rpi_orca_daemon/systemd/orca-haptics-daemon.service" "/etc/systemd/system/orca-haptics-daemon.service"

sudo systemctl daemon-reload
sudo systemctl enable orca-haptics-daemon
sudo systemctl restart orca-haptics-daemon

echo "Deploy complete. Check status with: systemctl status orca-haptics-daemon"
