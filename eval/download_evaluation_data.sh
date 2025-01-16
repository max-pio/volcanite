#!/bin/bash

if [ $# -eq 0 ]; then
    echo "Usage: $0 <directory_path> [--big-data]"
    echo "  <directory_path>: base directory in which data set sub-directories are stored"
    echo "  --big-data: if large (~ 2 TB) data sets will be downloaded as well. Use with care!"
    exit 1
fi

# create output directory in which the data set sub directories are stored
DIR="$1"
if [ ! -d "$DIR" ]; then
    mkdir -p "$DIR"
    echo "Directory $DIR created."
else
    echo "Directory $DIR already exists."
fi

# check if big data sets will be downloaded as well


# START DOWNLOADING

# AZBA
echo "---------- AZBA ----------"
if [ ! -d "$DIR" ]; then
  echo "downloading from https://datadryad.org/stash/downloads/file_stream/1098598"
  mkdir $DIR/azba/ && cd $DIR/azba
  wget https://datadryad.org/stash/downloads/file_stream/1098598
else
  echo "$DIR/azba/ already exists. Skipping AZBA."
fi