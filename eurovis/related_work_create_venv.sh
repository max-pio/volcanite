#!/bin/bash

sudo apt install python3-venv
python3 -m venv ./.venv
source ./.venv/bin/activate
pip install numpy pyvista h5py compresso pillow tdqm neuroglancer

echo 'Use "source .venv/bin/activate" to start venv'
