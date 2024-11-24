#!/bin/sh

####################################################
#          VOLCANITE EVALUATION SCRIPT             #
####################################################

VOLCANITE_DIR=~/code/volcanite/cmake-build-release
OUT_DIR=$(dirname $0)/out
CONFIG_DIR=$(dirname $0)/../config
echo "volcanite path ${VOLCANITE_DIR}/volcanite"
echo "output path ${OUT_DIR}"
echo "config path ${CONFIG_DIR}"

create_csv_tex_from_log()
{
  # prefix of "render.log" is "render". Script creates "render.csv" and "render.tex"
  PREFIX=$1
  echo "Creating ${PREFIX}.tex and ${PREFIX}.csv from ${PREFIX}.log"
  # create CSV file: remove comment lines, delete newlines \n, replace double slash \\ with newline
  cat ${PREFIX}.log | sed '/^#/d' | tr -d '\n' | sed 's/\\\\/\n/g' > ${PREFIX}.csv
  # create latex file: replace # with % as comment flag
  sed -e 's/^#/% /' ${PREFIX}.log > ${PREFIX}.tex  
}


# build most recent volcanite version
cd ${VOLCANITE_DIR}
git checkout mp/parallel-decode
cmake --build . -j 16

# name of the evaluation is the script name
NAME=${$(basename $0)%%.*}
EVAL_TMP_LOG=$(dirname $0)/${NAME}_tmp.log
EVAL_LOG=${OUT_DIR}/${NAME}.log

if [-f ${EVAL_LOG} ]; then
	echo "log file ${EVAL_LOG} already exists. aborting."
	exit(1)
fi

if [! -f ${VOLCANITE_DIR}/volcanite ]; then
	echo "volcanite executable ${VOLCANITE_DIR}/volcanite not found. aborting."
	exit(2)
fi

if [! -f ${EVAL_TMP_LOG} ]; then
	echo "evaluation template log file ${EVAL_TMP_LOG} not found. aborting."
	exit(3)
fi

# create the template evaluation log file for volcanite
cp ${EVAL_TMP_LOG} ${EVAL_LOG}

# remove the output file
rm ${OUT_DIR}/${NAME}_output.txt

cd ${OUT_DIR}

volcanite()
{
  echo "${VOLCANITE_DIR}/volcanite --eval-file ${EVAL_LOG} --verbose $@"
  echo "${VOLCANITE_DIR}/volcanite --eval-file ${EVAL_LOG} --verbose $@" >> ${OUT_DIR}/${NAME}_output.txt
  # ${VOLCANITE_DIR}/volcanite --eval-file ${EVAL_LOG} --verbose $@ >> ${OUT_DIR}/${NAME}_output.txt
  echo "1.9/4.9/6.2 & " >> ${EVAL_LOG}
}

####################################################

default="--cache-size 3000"
# NO CACHE -----------------------
echo "no cache" >> ${EVAL_LOG}
cache="--cache-mode n"
# rANS
echo " & - & - & - & -" >> ${EVAL_LOG}
# WM
mode="-s 2 -p -o pnl"
volcanite $mode $cache $default ~/data/cellsinsilico/Big01/000/outdir/cells_frame055.raw
volcanite $mode $cache $default ~/data/segmented_volumes/fiber
volcanite $mode $cache $default ~/data/h01_wm_pnl_h01_455.csgv
# WM + stop bits
mode="-s 2 -p -o a"
volcanite $mode $cache $default ~/data/cellsinsilico/Big01/000/outdir/cells_frame055.raw
volcanite $mode $cache $default ~/data/segmented_volumes/fiber
volcanite $mode $cache $default ~/data/h01_wm_455.csgv

# VOXEL CACHE -----------------------
echo "\\" >> ${EVAL_LOG}
echo "voxel cache" >> ${EVAL_LOG}
cache="--cache-mode v --empty-space-res 0"
# rANS
echo " & - & - & - & -" >> ${EVAL_LOG}
# WM
mode="-s 2 -p -o pnl"
volcanite $mode $cache $default ~/data/cellsinsilico/Big01/000/outdir/cells_frame055.raw
volcanite $mode $cache $default ~/data/segmented_volumes/fiber
volcanite $mode $cache $default ~/data/h01_wm_pnl_h01_455.csgv
# WM + stop bits
mode="-s 2 -p -o a"
volcanite $mode $cache $default ~/data/cellsinsilico/Big01/000/outdir/cells_frame055.raw
volcanite $mode $cache $default ~/data/segmented_volumes/fiber
volcanite $mode $cache $default ~/data/h01_wm_455.csgv

# VOXEL CACHE + EMPTY SPACE -----------------------
echo "\\" >> ${EVAL_LOG}
echo "voxel cache" >> ${EVAL_LOG}
cache="--cache-mode v --empty-space-res 2"
# rANS
echo " & - & - & - & -" >> ${EVAL_LOG}
# WM
mode="-s 2 -p -o pnl"
volcanite $mode $cache $default ~/data/cellsinsilico/Big01/000/outdir/cells_frame055.raw
volcanite $mode $cache $default ~/data/segmented_volumes/fiber
volcanite $mode $cache $default ~/data/h01_wm_pnl_h01_455.csgv
# WM + stop bits
mode="-s 2 -p -o a"
volcanite $mode $cache $default ~/data/cellsinsilico/Big01/000/outdir/cells_frame055.raw
volcanite $mode $cache $default ~/data/segmented_volumes/fiber
volcanite $mode $cache $default ~/data/h01_wm_455.csgv

# BRICK CACHE -----------------------
echo "\\" >> ${EVAL_LOG}
echo "brick cache" >> ${EVAL_LOG}
cache="--cache-mode b"
# rANS
mode="-s 2"
volcanite $mode $cache $default ~/data/cellsinsilico/Big01/000/outdir/cells_frame055.raw
volcanite $mode $cache $default ~/data/segmented_volumes/fiber
volcanite $mode $cache $default ~/data/h01_rANS_455.csgv
# WM
mode="-s 2 -p -o pnl"
volcanite $mode $cache $default ~/data/cellsinsilico/Big01/000/outdir/cells_frame055.raw
volcanite $mode $cache $default ~/data/segmented_volumes/fiber
volcanite $mode $cache $default ~/data/h01_wm_pnl_h01_455.csgv
# WM + stop bits
mode="-s 2 -p -o a"
volcanite $mode $cache $default ~/data/cellsinsilico/Big01/000/outdir/cells_frame055.raw
volcanite $mode $cache $default ~/data/segmented_volumes/fiber
volcanite $mode $cache $default ~/data/h01_wm_455.csgv

# BRICK CACHE (SHARED MEMORY) -----------------------
echo "brick cache (shared memory)" >> ${EVAL_LOG}
cache="--cache-mode n"
# rANS
echo " & - & - & - & -" >> ${EVAL_LOG}
# WM
mode="-s 2 -p -o pnl"
volcanite $mode $cache $default ~/data/cellsinsilico/Big01/000/outdir/cells_frame055.raw
volcanite $mode $cache $default ~/data/segmented_volumes/fiber
volcanite $mode $cache $default ~/data/h01_wm_pnl_h01_455.csgv
# WM + stop bits
mode="-s 2 -p -o a"
volcanite $mode $cache $default ~/data/cellsinsilico/Big01/000/outdir/cells_frame055.raw
volcanite $mode $cache $default ~/data/segmented_volumes/fiber
volcanite $mode $cache $default ~/data/h01_wm_455.csgv
echo "\\" >> ${EVAL_LOG}
