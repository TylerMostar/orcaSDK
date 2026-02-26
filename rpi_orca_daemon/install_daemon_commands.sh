cd ~/orcaSDK

cmake -S rpi_orca_daemon -B build/rpi_orca_daemon -DCMAKE_BUILD_TYPE=Release
cmake --build build/rpi_orca_daemon -j$(nproc)

sudo install -d /opt/orca-haptics/bin
sudo install -m 0755 build/rpi_orca_daemon/orca_haptics_daemon /opt/orca-haptics/bin/orca_haptics_daemon

sudo tee /etc/systemd/system/orca-haptics-daemon.service > /dev/null <<'EOF'
[Unit]
Description=ORCA multi-actuator haptics daemon
After=network-online.target
Wants=network-online.target

[Service]
Type=simple
User=pi
Group=dialout
WorkingDirectory=/opt/orca-haptics
ExecStart=/opt/orca-haptics/bin/orca_haptics_daemon --device /dev/USB_serial_converter_1 --baud 19200 --interframe-us 2000 --damping 600 --open-retry-ms 50 --configure-retry-ms 20 --health-check-ms 100
Restart=always
RestartSec=1

[Install]
WantedBy=multi-user.target
EOF

sudo systemctl daemon-reload
sudo systemctl enable orca-haptics-daemon
sudo systemctl restart orca-haptics-daemon

systemctl status orca-haptics-daemon --no-pager -l
journalctl -u orca-haptics-daemon -n 100 --no-pager