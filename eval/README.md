# Volcanite Evaluation Scripts

This directory contains several evaluation python scripts.
The structure of the directory is as follows:
* `./config/` contains the `.vcfg` and `.rec` files for the renderer
* `./results/` will contain the results of the evaluation scripts 
* `./setup.txt` is created by the data downloader and contains file paths to this directory and the volcanite binary

## Prerequisities 

The evaluation scripts require python and the Volcanite python package located in `../python/volcanite/` to be installed.
You need the complete Volcanite source directory and all [build dependencies](../doc/Setup.md), including the optional hdf5 libraries   
It is advised to do this inside a python virtual environment:

```bash
python -m venv ./.venv
source .venv/bin/activate
pip install --upgrade pip
pip install ../python/volcanite[all]
```

### Data Set Download

The `download_evaluation_data.py` script downloads all evaluation data and uses Volcanite to compress them into CSGV
files. The script takes one argument `<directory>` to specify where the data should be stored. This should be a non-existing or empty
directory:

```bash
python3 download_evaluation_data.py /your/data/dir
```
The original data sets are downloaded into subdirectories of `<directory>`, the CSGV files are placed directly inside.
The script will also create a file `./setup.txt` in which the paths to the volcanite source directory and `<directory>`
is stored for the evaluation scripts.

In addition, the following arguments exist as well:
* `--keep` do not remove the original input data once the CSGV files are created
* `--big-data` downloads and compresses large, chunked data sets (~2 TB)
* `--volcanite-src` if the download script is not run from inside the volcanite directory, 
