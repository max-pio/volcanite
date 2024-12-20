#!/bin/bash

echo "Which file should be used for the benchmark"
read INPUT_FILE
echo "Choose (unique) identifier for perf.data files"
read IDENTIFIER

CUR_PATH=$(dirname "$0")
for i in $(seq 0 100 500)
do
    echo "$CUR_PATH"/benchmark $INPUT_FILE -q "$i"M
    #perf stat -e cache-references,cache-misses,cycles,instructions,branches,faults,migrations -B -- ./benchmark $INPUT_FILE -q "$i"M
    perf record -B -e cache-references,cache-misses,cycles,instructions,branches,faults,migrations -- ./benchmark $INPUT_FILE -q "$i"M
    mv perf.data perf_"$IDENTIFIER"_"$i".data
done
