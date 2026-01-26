#!/bin/bash

source ../.venv/bin/activate

python3 plt-shading-times.py
python3 plt-gpu-times.py
python3 plt-postprocess-times.py
python3 plt-vtk.py