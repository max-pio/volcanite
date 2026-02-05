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

echo "activating python venv in ./.venv/bin/activate"
source ./.venv/bin/activate

# create timestamped logfile name
timestamp="$(date +'%Y-%m-%d_%H-%M-%S')"
logfile="eval-log-${timestamp}.log"
# redirect all outputs to the logfile (and keep on console)
echo "Redirecting all logging outputs to ${logfile}"
echo "To view live logs, execute in another shell:"
echo "tail -f ${logfile}"
exec > "$logfile" 2>&1
# to receive console output as well: use tee (might impact performance)
# exec > >(stdbuf -oL tee -a "$logfile") 2>&1


# calling python scripts unbuffered (-u) to prevent output mixing

# The following evaluations pose significant stress on IO since they involve
# many CSGV compressions. At the same time, their results do not depend on
# CPU / GPU performance and can therefore be executed on a different system
# (i.e. a server with higher memory resources)
echo "<<<<<<< csgv-ablation-eval.py >>>>>>>"
python3 -u ./csgv-ablation-eval.py
sleep 30
echo "<<<<<<< deltaoperation-b64-eval.py >>>>>>>"
python3 -u ./deltaoperation-b64-eval.py

echo ""
echo "-------------------"
echo "all done."
