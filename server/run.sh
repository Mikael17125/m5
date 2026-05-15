#!/bin/bash
cd "$(dirname "$0")"
echo "=== M5 HUB Daemon ==="
echo "Started: $(date)"
echo "PID: $$"
echo "Log: ble_daemon.log"
echo ""
python ble_daemon.py 2>&1 | tee ble_daemon.log
