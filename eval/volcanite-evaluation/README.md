# Volcanite Evaluation Scripts

This directory contains several evaluation python scripts that generate the results for the main Volcanite paper.
The structure of the directory is as follows:
* `./` The root contains all python scripts for executing evaluations and downloading input data,
* `./volcanite-eval-setup.txt` is created by the data downloader and contains file paths to this directory and the volcanite binary,
* `./config/` contains the `.vcfg` and `.rec` files for the renderer,
* `./results/` will contain the results of the evaluation scripts,
* `./plots/` contains scripts for creating all results plots in PDF format inside the results directory.

It is advisable to place this directory in the 

## System Requirements
**Important: The evaluations can pose heavy loads on GPU, RAM, and disk storage.
We take no responsibility for any problems this might cause.** 

* AMD or NVIDIA GPU with at least 16 GB VRAM,
* 48 GB RAM,
* approx. 1 TB free disk space.

You might be able to run a subset of evaluation scripts on a subset of data sets if your system does not meet these requirements.


## Prerequisities
The Volcanite git source must be available in `<volcanite-src-root-dir>`.
If not already present, first clone the Volcanite repository:
```
git clone git@github.com:max-pio/volcanite.git
```
And place this volcanite-evalution directory inside its source tree.
We recommend `<volcanite-root-src-dir>/eval/`.

You need all Volcanite [build dependencies](../doc/Setup.md), including the optional hdf5 libraries.
The evaluation scripts require python and the Volcanite python package located in `<volcanite-src-root-dir>/python/volcanite/` to be installed. 
It is advised to do this inside a python virtual environment:

```bash
python -m venv ./.venv
source .venv/bin/activate
pip install --upgrade pip
pip install pandas numpy matplotlib
pip install <volcanite-src-root-dir>/python/volcanite[all]
```

### Data Set Download

The `download_evaluation_data.py` script downloads all evaluation data and uses Volcanite to compress them into CSGV
files. The script takes one argument `<directory>` to specify where the data should be stored. 
This should be a non-existing or empty directory.
All data is downloaded from publicly available repositories, license information is provided accordingly.

The original data sets are downloaded into subdirectories of `<directory>`, the CSGV files are placed directly inside the root.
The script will also create a file `./volcanite-eval-setup.txt` in which the paths to the volcanite source directory and `<directory>`
is stored for the evaluation scripts.

In addition, the following arguments exist as well:
* `--keep` Will keep the original input data once the CSGV files are created (~1 TB extra memory).
* `--big-data` Downloads and compresses large, chunked data sets (~1 TB extra memory)
* `--volcanite-src` If the download script is not run from inside the volcanite source tree, the path to the volcanite source root must be specified.
* `--overwrite` Overwrites previously created data.
* `--preview` Will create preview images of the data sets as well.
* `--no-abort` Download other data sets even if another download fails.
* `--only <dataset>` Only download a single specified data set.
* `--single-chunk-copy` Create single chunked raw data copies of smaller chunked data sets. 

To be able to execute all evaluation scripts, the following arguments are mandatory, (except `--preview` and `--no-abort` which are just recommended):  
```bash
python3 download_evaluation_data.py /your/data/dir --big-data --keep --single-chunk-copy --preview --no-abort
```
 
The evaluations assume a system with at least 64 GB RAM and a GPU with at least 16 GB VRAM.
If your system does not meet these requirements, consider downloading only the smaller data
(i.e. omitting `--big-data` in the data set download script).
Some evaluations will create many large files (videos, compressed volumes, etc.) in `./results`.
Make sure that enough free disk space is available.

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

For convenience, a shell script (Ubuntu Linux) executes all evaluations:
```
./run-all.sh
```
If entry- or exit-commands require root privileges, execute ./run-all.sh with sudo as well.  

Single scripts can be executed as:
```
python3 image-eval.py
```

Afterward, results can be found in the [results/](./results) subdirectory.
If not all data sets could be downloaded or were requested for download, some result tables may return missing entries.
In general, the scripts should ignore evaluation runs that fail due to non-existing data sets.

## Plotting
After gathering all results, the plots can be created with the scripts in [plots/](./plots).
Again, a shell script `./plt-all.sh` will create all plots.
Afterward, the PDF plot files are found in [results/plots/](./results/plots).
