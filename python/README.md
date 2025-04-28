# PyVolcanite

This python package contains a simple interface to run the Volcanite executable through system calls and manage
Volcanite command line arguments.
The main focus of the interface is running evaluation scripts and gathering the evaluation results in formatted text files.
Additionally, the package includes utility functions to convert segmentation volume files between different formats and chunk
sizes, and to download example data sets from online cloud storages.

## Install Instructions

It is advised to install the package within a [python virtual environment (venv)](https://docs.python.org/3/library/venv.html):

```bash
python -m venv .venv
source .venv/bin/activate
```

Install the volcanite package. If you prefer to not install any optional dependencies or only those for a specific
functionality, install `./volcanite`, or `./volcanite[converter]` instead.
```bash
pip install --upgrade pip
pip install ./volcanite[all]
```

## Script Usage

Use `python3 convert.py -h` to convert segmentation volume between different file types:
```bash
python3 convert.py ./my_volume.nii ./my_volume.hdf5
```

Use `python3 download_cloud_data.py -h` to download segmentation volume subsets from cloud storages:
```bash
python3 download_cloud_data.py h01 -s 128 128 128 -d ./h01_test/
```


## Packaging Instructions

To build the volcanite package for distribution, first upgrade your build tools to the newest version.
Then build the package from the volcanite directory.

```bash
python3 -m pip install --upgrade build
pip install --upgrade pip
cd volcanite
python3 -m build 
```
