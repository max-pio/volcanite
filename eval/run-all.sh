#!/bin/bash

if [ "$(id -u)" -ne 0 ]; then
  echo "This script should be run as root (use sudo) when using [entry-|exit-]commands for locking GPU clocks."
  read -p "Continue as non-root? [y/N] " response
  case "$response" in
    [yY][eE][sS]|[yY])
      echo "Continuing as non-root..."
      ;;
    *)
      echo "Exiting."
      exit 1
      ;;
  esac
fi

source ../.venv/bin/activate

python3 ./image-eval.py
sleep 30
python3 ./video-eval.py
sleep 30
python3 ./vram-eval.py
sleep 30
python3 ./resolve-video-eval.py
sleep 30

python3 ./cache-palette-eval.py
sleep 30
python3 ./cache-pathlimit-eval.py
sleep 30
python3 ./cache-reqlimit-eval.py
sleep 30
python3 ./cache-rngcontrol-eval.py
sleep 30

python3 ./csgv-eval.p
sleep 30
python3 ./csgv-ablation-eval.py
sleep 30
python3 ./deltaoperation-b64-eval.py

echo "all finished."
