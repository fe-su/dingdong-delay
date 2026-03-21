#!/bin/bash
# install.sh — DingDong Delay LV2 -pluginin asennus Raspberry Pi:lle
set -e
echo "=== DingDong Delay LV2 — asennusskripti ==="
sudo apt-get update -qq
sudo apt-get install -y lv2-dev gcc make
make clean && make
make install
sudo systemctl restart pipedald
echo "=== Valmis! Etsi 'DingDong Delay' PiPedalista. ==="
