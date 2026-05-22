# Volcanite Evaluation Scripts

This directory contains several evaluation python scripts that generate the results for the main Volcanite paper.
The structure of the directory is as follows:
* `./` The root contains all python scripts for executing evaluations and downloading input data,
* `./volcanite-eval-setup.txt` is created by the data downloader and contains file paths to this directory and the volcanite binary,
* `./config/` contains the `.vcfg` and `.rec` files for the renderer,
* `./results/` will contain the results of the evaluation scripts,
* `./plots/` contains scripts for creating all results plots in PDF format inside the results directory.

It is advisable to place this directory in the `<volcanite-root-src-dir>/eval/` subdirectory of the cloned Volcanite repository. 

## System Requirements
**Important: The evaluations can pose heavy loads on GPU, RAM, and disk storage.
We take no responsibility for any problems this might cause.** 

* AMD or NVIDIA GPU with at least 16 GB VRAM,
* 48 GB RAM,
* approx. 1 TB free disk space.

The evaluation runs were only tested with Ubuntu 24.04 and some shell scripts will not work on Windows systems. 
You might be able to run a subset of evaluation scripts on a subset of data sets if your system does not meet these requirements.


## Prerequisities
The Volcanite git source must be available in `<volcanite-src-root-dir>`.
If not already present, first clone the Volcanite repository:
```
git clone git@github.com:max-pio/volcanite.git
```
And place this volcanite-evalution directory inside its source tree.
We recommend to place it in `<volcanite-root-src-dir>/eval/`.

You need to install the Volcanite build dependencies (see its [doc/Setup.md](https://github.com/max-pio/dev-volcanite/blob/main/doc/Setup.md)), including the optional hdf5 libraries.
The evaluation scripts require python and the Volcanite python package located in `<volcanite-src-root-dir>/python/volcanite/` to be installed.
Volcanite must be built with `Release` build type in `<volcanite-src-root-dir>/cmake-build-release`:
```bash
cd <volcanite-src-root-dir>
mkdir cmake-build-release && cd cmake-build-release
cmake -DCMAKE_BUILD_TYPE=Release .. && cmake --build . -j --target volcanite
```

Install python dependencies. On Ubuntu:
```bash
sudo apt update && sudo apt install python3-venv python3-dev build-essential
```

Install packages inside a local python virtual environment:
```bash
python3 -m venv ./.venv
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
* `--big-data` Downloads and compresses large, chunked data sets (~1 TB extra memory).
* `--volcanite-src` If the download script is not run from inside the volcanite source tree, the path to the volcanite source root must be specified.
* `--overwrite` Overwrites previously created data.
* `--preview` Will create preview images of the data sets as well.
* `--no-abort` Download other data sets even if another download fails.
* `--only <dataset>` Only download a single specified data set.
* `--single-chunk-copy` Create single chunked raw data copies of smaller chunked data sets. 

To be able to execute *all* evaluation scripts, the following arguments are mandatory, (except `--preview` and `--no-abort` which are just recommended):
```bash
python3 download_evaluation_data.py /your/data/dir --big-data --keep --single-chunk-copy --preview --no-abort
```
 
The evaluations assume a system with at least 48 GB RAM, a GPU with at least 16 GB VRAM, and at least 1 TB of free storage space in the data download directory.
If your system does not meet these requirements, consider downloading only the smaller data
(i.e. omitting `--big-data` in the data set download script).
Some evaluations will create many large files (videos, compressed volumes, etc.) in `./results`.
Make sure that enough free disk space is available on the drive where the evaluation directory located.

### Optional: Fixing GPU clock speeds

For most accurate render timing measurements, it is optionally recommended to fix performance counters and GPU and VRAM clock speeds before each evaluation run.
Use these at your own risk.
To that end, the [volcanite-eval-setup.txt](volcanite-eval-setup.txt) allows to set entry and exit shell commands that are execute before and after an evaluation script is run respectively.

Example for an NVIDIA GPU with a maximum GPU clock speed of 3105 MHz and a maximum memory clock speed of 10501 MHz:
```bash
entry-command: sudo nvidia-smi --lock-gpu-clocks=3105 && sudo nvidia-smi --lock-memory-clocks=10501 && sudo nvidia-smi -pm 1
exit-command: sudo nvidia-smi --reset-gpu-clocks && sudo nvidia-smi --reset-memory-clocks && sudo nvidia-smi -pm 0
```

## Running the Evaluations

Evaluation scripts are named `*-eval.py` and located in this directory.
They can be directly executed in the previously created virtual environment.
To that end, run a single evaluation with:
```bash
source .venv/bin/activate
python3 image-eval.py
```

For convenience, a shell script (Ubuntu Linux) executes all evaluations
```bash
./run-all.sh
```
except csgv-ablation-eval.py and deltaoperation-b64-eval.py which put excessive strain on storage drives through heavy read/write usage and storage output.
These can be run through `./run-ablation-delta.sh`.

If entry- or exit-commands require root privileges (e.g. fixing GPU clocks), execute ./run-all.sh with sudo as well.

Afterward, results can be found in the [results/](./results) subdirectory.
If not all data sets could be downloaded or were requested for download, some result tables may return missing entries.
In general, the scripts should ignore evaluation runs that fail due to non-existing data sets.

## Plotting
After gathering all results, the plots can be created with the scripts in [plots/](./plots).
Again, a shell script `./plt-all.sh` will create all plots.
Afterward, the PDF plot files are found in [results/plots/](./results/plots).
