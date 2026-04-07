#!/bin/bash
set -e

echo "=== DingDong Delay — installer ==="

sudo apt-get update -qq
sudo apt-get install -y lv2-dev gcc make

make clean && make deploy

echo "=== Done! Search for 'DingDong Delay' in PiPedal. ==="
