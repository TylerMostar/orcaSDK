cd ~/Desktop/OrcaSDK_Daemon_repo/orcaSDK

cmake -S rpi_orca_daemon -B build/rpi_orca_daemon -DCMAKE_BUILD_TYPE=Release
cmake --build build/rpi_orca_daemon -j$(nproc)

sudo install -m 0644 rpi_orca_daemon/systemd/orca-haptics-daemon.service /etc/systemd/system/orca-haptics-daemon.service

sudo systemctl daemon-reload
sudo systemctl enable orca-haptics-daemon
sudo systemctl restart orca-haptics-daemon

systemctl status orca-haptics-daemon --no-pager -l
journalctl -u orca-haptics-daemon -n 100 --no-pager
