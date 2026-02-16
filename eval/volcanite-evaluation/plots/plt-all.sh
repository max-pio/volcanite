#!/bin/bash

source ../.venv/bin/activate

# plots
python3 plt-bits-per-voxel.py
python3 plt-compression-rate.py
python3 plt-csgv-ablation.py
python3 plt-palette-delta.py

python3 plt-gpu-times.py
python3 plt-shading-times.py
python3 plt-postprocess-times.py

python3 plt-vtk.py
python3 plt-preprocess-tools.py

# tables
python3 tab-preprocess-tools.py
