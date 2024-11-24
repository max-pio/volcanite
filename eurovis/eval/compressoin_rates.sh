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
  echo "${VOLCANITE_DIR}/volcanite --headless --eval-file ${EVAL_LOG} --verbose $@"
  echo "${VOLCANITE_DIR}/volcanite --headless --eval-file ${EVAL_LOG} --verbose $@" >> ${OUT_DIR}/${NAME}_output.txt
  # ${VOLCANITE_DIR}/volcanite --headless --eval-file ${EVAL_LOG} --verbose $@ >> ${OUT_DIR}/${NAME}_output.txt
  echo "1.9/4.9/6.2 & " >> ${EVAL_LOG}
}

####################################################

# within the row: Nibble, rANS, WM, WM sb

# CELLS -----------------------
data="~/data/cellsinsilico/Big01/000/outdir/cells_frame055.raw"
echo "\\midrule" >> ${EVAL_LOG}
echo "\\multirow{3}{*}{\\rotatebox{90}{\\cells{}}}" >> ${EVAL_LOG}
# b=16
bricksize=16
echo " & \$${bricksize}\$ " >> ${EVAL_LOG}
volcanite ${data} b=${bricksize} -s 0
volcanite ${data} b=${bricksize} -s 2
volcanite ${data} b=${bricksize} -s 2 -p -o pnl
volcanite ${data} b=${bricksize} -s 2 -p -o a
echo "\\" >> ${EVAL_LOG}
# b=32
bricksize=32
echo " & \$${bricksize}\$ " >> ${EVAL_LOG}
volcanite ${data} b=${bricksize} -s 0
volcanite ${data} b=${bricksize} -s 2
volcanite ${data} b=${bricksize} -s 2 -p -o pnl
volcanite ${data} b=${bricksize} -s 2 -p -o a
echo "\\" >> ${EVAL_LOG}
# b=64
bricksize=64
echo " & \$${bricksize}\$ " >> ${EVAL_LOG}
volcanite ${data} b=${bricksize} -s 0
volcanite ${data} b=${bricksize} -s 2
volcanite ${data} b=${bricksize} -s 2 -p -o pnl
volcanite ${data} b=${bricksize} -s 2 -p -o a
echo "\\" >> ${EVAL_LOG}

# FIBER -----------------------
data="~/data/fiber.hdf5"
echo "\\midrule" >> ${EVAL_LOG}
echo "\\multirow{3}{*}{\\rotatebox{90}{\\fiber{}}}" >> ${EVAL_LOG}
# b=16
bricksize=16
echo " & \$${bricksize}\$ " >> ${EVAL_LOG}
volcanite ${data} b=${bricksize} -s 0
volcanite ${data} b=${bricksize} -s 2
volcanite ${data} b=${bricksize} -s 2 -p -o pnl
volcanite ${data} b=${bricksize} -s 2 -p -o a
echo "\\" >> ${EVAL_LOG}
# b=32
bricksize=32
echo " & \$${bricksize}\$ " >> ${EVAL_LOG}
volcanite ${data} b=${bricksize} -s 0
volcanite ${data} b=${bricksize} -s 2
volcanite ${data} b=${bricksize} -s 2 -p -o pnl
volcanite ${data} b=${bricksize} -s 2 -p -o a
echo "\\" >> ${EVAL_LOG}
# b=64
bricksize=64
echo " & \$${bricksize}\$ " >> ${EVAL_LOG}
volcanite ${data} b=${bricksize} -s 0
volcanite ${data} b=${bricksize} -s 2
volcanite ${data} b=${bricksize} -s 2 -p -o pnl
volcanite ${data} b=${bricksize} -s 2 -p -o a
echo "\\" >> ${EVAL_LOG}

# H01 -----------------------
data="~/data/h01.hdf5"
echo "\\midrule" >> ${EVAL_LOG}
echo "\\multirow{3}{*}{\\rotatebox{90}{\\hone{}}}" >> ${EVAL_LOG}
data="~/data/fiber.hdf5"
echo "\\midrule" >> ${EVAL_LOG}
echo "\\multirow{3}{*}{\\rotatebox{90}{\\fiber{}}}" >> ${EVAL_LOG}
# b=16
bricksize=16
echo " & \$${bricksize}\$ " >> ${EVAL_LOG}
volcanite ${data} b=${bricksize} -s 0
volcanite ${data} b=${bricksize} -s 2
volcanite ${data} b=${bricksize} -s 2 -p -o pnl
volcanite ${data} b=${bricksize} -s 2 -p -o a
echo "\\" >> ${EVAL_LOG}
# b=32
bricksize=32
echo " & $${bricksize}$ " >> ${EVAL_LOG}
volcanite ${data} b=${bricksize} -s 0
volcanite ${data} b=${bricksize} -s 2
volcanite ${data} b=${bricksize} -s 2 -p -o pnl
volcanite ${data} b=${bricksize} -s 2 -p -o a
echo "\\" >> ${EVAL_LOG}
# b=64
bricksize=64
echo " & $${bricksize}$ " >> ${EVAL_LOG}
volcanite ${data} b=${bricksize} -s 0
volcanite ${data} b=${bricksize} -s 2
volcanite ${data} b=${bricksize} -s 2 -p -o pnl
volcanite ${data} b=${bricksize} -s 2 -p -o a
echo "\\" >> ${EVAL_LOG}

# AZBA -----------------------
data="~/data/azba.hdf5"
echo "\\midrule" >> ${EVAL_LOG}
echo "\\multirow{3}{*}{\\rotatebox{90}{\\azba{}}}" >> ${EVAL_LOG}
data="~/data/fiber.hdf5"
echo "\\midrule" >> ${EVAL_LOG}
echo "\\multirow{3}{*}{\\rotatebox{90}{\\fiber{}}}" >> ${EVAL_LOG}
# b=16
bricksize=16
echo " & \$${bricksize}\$ " >> ${EVAL_LOG}
volcanite ${data} b=${bricksize} -s 0
volcanite ${data} b=${bricksize} -s 2
volcanite ${data} b=${bricksize} -s 2 -p -o pnl
volcanite ${data} b=${bricksize} -s 2 -p -o a
echo "\\" >> ${EVAL_LOG}
# b=32
bricksize=32
echo " & \$${bricksize}\$ " >> ${EVAL_LOG}
volcanite ${data} b=${bricksize} -s 0
volcanite ${data} b=${bricksize} -s 2
volcanite ${data} b=${bricksize} -s 2 -p -o pnl
volcanite ${data} b=${bricksize} -s 2 -p -o a
echo "\\" >> ${EVAL_LOG}
# b=64
bricksize=64
echo " & \$${bricksize}\$ " >> ${EVAL_LOG}
volcanite ${data} b=${bricksize} -s 0
volcanite ${data} b=${bricksize} -s 2
volcanite ${data} b=${bricksize} -s 2 -p -o pnl
volcanite ${data} b=${bricksize} -s 2 -p -o a
echo "\\" >> ${EVAL_LOG}
