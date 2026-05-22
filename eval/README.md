# Volcanite Evaluation Scripts

Python evaluation scripts are used to execute Volcanite through its headless CLI and track results in log files.

It is advisable to store all scripts and data for evaluating a certain project in one subdirectory.
The structure of such a directory could be as follows:
* `./config/` contains the `.vcfg` and `.rec` files for the renderer
* `./results/` will contain the results of the evaluation scripts 
* `./volcanite-eval-setup.txt` is created by the data downloader and contains file paths to this directory and the volcanite binary.

## Prerequisities
The evaluation scripts require python and the Volcanite python package located in `../python/volcanite/` to be installed.
You need the complete Volcanite source directory and all [build dependencies](../doc/Setup.md).
It is advised to do this inside a python virtual environment in the evaluation directory, for example `my-eval`:

```bash
cd my-eval
python -m venv ./.venv
source .venv/bin/activate
pip install --upgrade pip
pip install ../../python/volcanite[all] pandas
```

### Data Set Download

Segmentation volumes can be downloaded from a variety of domains and online storages:
* https://bossdb.org/
* https://datadryad.org/

The [python/download_cloud_data.py](../python/download_cloud_data.py) python script can be used to download (chunks) of large scale cloud volumes.
All data sets for the evaluation should be converted into .csgv files with Volcanite and stored in a single common directory `<data-directory>`.
The name of a .csgv file will determine the data sets respective identifier for the evaluations.
When providing default .vcfg rendering configurations for each data sets, these should have the same identifier names
and be stored in a single directory `<vcfg-directory>`.

### Creating the volcanite-eval-setup.txt 
The evaluation scripts can automatically detect available data sets from `<data-directory>/<identifier>.csgv` files
as well as their respective default .vcfg configurations from `<vcfg-directory>/<identifier>.vcfg` files.
Additionally, they can perform git checkout and build of the `<volcanite-source-directory>` (git project base directory).

To that end, these paths must be provided in a file `volcanite-eval-setup.txt`.
This file must be located in the working directory during script execution and is formated as:
```
volcanite_src: /home/maxpio/code/volcanite
config_dir: /home/maxpio/code/volcanite/eval/config
csgv_dir: /media/maxpio/data/eval
```
Optionally, the entries `entry-command` and `exit-command` can specify shell commands that will be executed before and after an evaluation is executed respectively.

#### Optional: Fixing GPU clock speeds
The `entry-command` and `exit-command` are most useful for fixing GPU clock speeds: 
For most accurate render timing measurements, it is recommended to fix performance counters and GPU and VRAM clock speeds before each evaluation run.

Example for an NVIDIA GPU with a maximum GPU clock speed of 3105 MHz and a maximum memory clock speed of 10501 MHz:
```
entry-command: sudo nvidia-smi --lock-gpu-clocks=3105 && sudo nvidia-smi --lock-memory-clocks=10501 && sudo nvidia-smi -pm 1
exit-command: sudo nvidia-smi --reset-gpu-clocks && sudo nvidia-smi --reset-memory-clocks && sudo nvidia-smi -pm 0
```

## Running an Evaluation

See [test-eval.py](test-eval.py) for an example evaluation script.
Run the evaluations from the same .venv:
```
source .venv/bin/activate
python3 test-eval.py
```

First, a `VolcaniteEvaluation` is created that manages the overall evaluation run.
Then, a list of `VolcaniteArg` is created that translates configurations into CLI arguments and provides (iterable) default parameters.
Finally, Volcanite is executed by the `VolcaniteExec` object which handles all execution calls to git, CMake builds or Volcanite itself.

```python
# 1. Create Volcanite Evaluation
with VolcaniteEvaluation(eval_out_directory="./my_test_eval", existing_policy=ExistingPolicy.DELETE,
                                    eval_name="my_test_eval",
                                    log_files=[VolcaniteLogFileCfg("results.txt",
                                                          fmts=["{name},{comprate_pcnt:.3},{frame_avg_ms}"],
                                                          headers=["Name,Compression Rate [%],frame avg [ms]"])],
                                    enable_log=True, dry_run=False) as evaluation:
    # Build volcanite
    volcanite = VolcaniteExec(evaluation, build_subdir="cmake-build-release")
    volcanite.checkout_and_build()

    # 2. Select Volcanite Arguments for the evaluation run
    vargs = [VolcaniteArg.args_datasynth["dSynth32"],
             VolcaniteArg.args_encoding["rANS"]]
    # Render a single image to a file to gather average frame timings. 
    # The image filename is constructed from the short IDs of previous arguments.
    vargs.append(VolcaniteArg.arg_image_export(vargs))
    
    # 3. Execute Volcanite and export evaluation results
    volcanite.exec(vargs, eval_name="Synthetic Volume")

```

### Volcanite Evaluation Log Files
Volcanite appends evaluation results after each execution to log files that are passed via the command line argument `--eval-log-files`.
The log file must contain one or more leading rows starting with `#fmt:` followed by a formatted string.
Volcanite uses this string to format the output text of the evaluation based on a set of predefined keys (see `--eval-print-keys`).
Keys have are in curly braces and can be formatted following the C++ formatting (see: https://hackingcpp.com/cpp/libs/fmt.html). 
The special key `{name}` takes the value of the command line argument `--eval-name`.

The setup of such logfiles and format strings can be handled by the `VolcaniteEvaluation` (`log_files=[VolcaniteLogFileCfg(..), ...]`).
The evaluation `{name}` can be set for each individual Volcanite execution via the `VolcaniteExec` object.
Note: the special argument `--timings-logfile` triggers the export of individual frame (GPU) rendering timings in a non-configurable.csv format.

## Volcanite Main Paper Evaluation
The evaluations to generate the results of the Volcanite main publication can be found in the [volcanite-evaluation](https://github.com/max-pio/volcanite-evaluation) repository.
Simply clone the repository into this directory:
```bash
git clone git@github.com:max-pio/volcanite-evaluation.git
```
