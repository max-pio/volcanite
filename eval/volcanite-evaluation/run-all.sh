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

# create timestamped logfile name
timestamp="$(date +'%Y-%m-%d_%H-%M-%S')"
logfile="eval-log-${timestamp}.log"
# redirect all outputs to the logfile (and keep on console)
exec > "$logfile" 2>&1
# to receive console output as well: use unbuffered tee
# exec > >(stdbuf -oL tee -a "$logfile") 2>&1


echo "<<<<<<< image-eval.py >>>>>>>"
python3 ./image-eval.py
sleep 30
echo "<<<<<<< video-eval.py >>>>>>>"
python3 ./video-eval.py
sleep 30
echo "<<<<<<< vram-eval.py >>>>>>>"
python3 ./vram-eval.py
sleep 30
echo "<<<<<<< resolve-eval.py >>>>>>>"
python3 ./resolve-video-eval.py
sleep 30

echo "<<<<<<< cache-palette-eval.py >>>>>>>"
python3 ./cache-palette-eval.py
sleep 30
echo "<<<<<<< cache-pathlimit-eval.py >>>>>>>"
python3 ./cache-pathlimit-eval.py
sleep 30
echo "<<<<<<< cache-reqlimit-eval.py >>>>>>>"
python3 ./cache-reqlimit-eval.py
sleep 30
echo "<<<<<<< cache-rngcontrol-eval.py >>>>>>>"
python3 ./cache-rngcontrol-eval.py
sleep 30

echo "<<<<<<< csgv-eval.py >>>>>>>"
python3 ./csgv-eval.py
sleep 30
echo "<<<<<<< csgv-ablation-eval.py >>>>>>>"
python3 ./csgv-ablation-eval.py
sleep 30
echo "<<<<<<< deltaoperation-b64-eval.py >>>>>>>"
python3 ./deltaoperation-b64-eval.py

echo ""
echo "-------------------"
echo "all done."
