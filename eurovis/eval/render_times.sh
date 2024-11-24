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
  echo "${PREFIX}"
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
EVAL_TMP=$(dirname $0)/${NAME}_tmp.log

if [-f ${OUT_DIR}/${NAME} ]; then
	echo "log file ${OUT_DIR}/${NAME} already exists. aborting."
	exit(1)
fi

if [! -f ${VOLCANITE_DIR}/volcanite ]; then
	echo "volcanite executable ${VOLCANITE_DIR}/volcanite not found. aborting."
	exit(2)
fi

if [! -f ${EVAL_TMP} ]; then
	echo "evaluation template log file ${EVAL_TMP} not found. aborting."
	exit(3)
fi

# create the template evaluation log file for volcanite
cp EVAL_TMP ${OUT_DIR}/${NAME}.log

cd ${OUT_DIR}
####################################################

# ${VOLCANITE_DIR}/volcanite --eval-file ${OUT_DIR}/${NAME}
# create_csv_tex_from_log ${OUT_DIR}/${NAME}

