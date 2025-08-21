# Volcanite Evaluation Scripts

This directory contains several evaluation python scripts that generate the results for the main Volcanite paper.

For importing the evaluation data sets, Volcanite must be build with the hdf5 libraries being available.
The evaluations assume a system with at least 64 GB RAM and a GPU with at least 16 GB VRAM.
If your system does not meet this requirements, consider downloading only the smaller data
(i.e. omitting `--big-data` in the data set download script). 
 

The structure of the directory is as follows:
* `./config/` contains the `.vcfg` and `.rec` files for the renderer
* `./results/` will contain the results of the evaluation scripts 
* `./volcanite-eval-setup.txt` is created by the data downloader and contains file paths to this directory and the volcanite binary.

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
* `--big-data` downloads and compresses large, chunked data sets (~1 TB)
* `--volcanite-src` if the download script is not run from inside the volcanite directory, 


### Optional: Fixing GPU clock speeds

For most accurate render timing measurements, it is optionally recommended to fix performance counters and GPU and VRAM clock speeds before each evaluation run.
To that end, the [volcanite-eval-setup.txt](volcanite-eval-setup.txt) allows to set an entry- and shell exit-command that are execute before and after an evaluation script is run respectively.

Example for an NVIDIA GPU with a maximum GPU clock speed of 3105 MHz and a maximum memory clock speed of 10501 MHz:
```
entry-command: sudo nvidia-smi --lock-gpu-clocks=3105 && sudo nvidia-smi --lock-memory-clocks=10501 && sudo nvidia-smi -pm 1
exit-command: sudo nvidia-smi --reset-gpu-clocks && sudo nvidia-smi --reset-memory-clocks && sudo nvidia-smi -pm 0
```

## Running the Evaluations

Evaluation scripts are named `*-eval.py` and located in this directory.
They can be directly executed in the previously created virtual environment.
For example:
```
python3 test-eval.py
```
Afterward, results can be found in the [/results](./results) subdirectory.

If not all data sets could be downloaded or were requested for download, some result tables may return missing entries.
In general, the scripts should ignore evaluation runs that fail due to non-existing data sets.