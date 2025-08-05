#!/bin/bash

if [ "$(id -u)" -ne 0 ]; then
  echo "This script must be run as root (use sudo)" 1>&2
  exit 1
fi

source ../.venv/bin/activate

python3 ./image-eval.py
sleep 30
python3 ./video-eval.py
sleep 30
python3 ./vram-eval.py
sleep 30

python3 ./csgv-eval.p
sleep 30
python3 ./deltaoperation-eval.py
sleep 30

python3 ./cache-palette-eval.py 
sleep 30
python3 ./cache-pathlimit-eval.py
sleep 30
python3 ./cache-reqlimit-eval.py
sleep 30
python3 ./cache-rngcontrol-eval.py

