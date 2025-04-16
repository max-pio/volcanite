# Volcanite Python Execution and Evaluation

Volcanite provides command line arguments for exporting results from compression or rendering passes to log files. 
The `volcaniteeval` module provides utility classes for evaluation certain aspects of Volcanite through python scripting.

## Volcanite CLI Result Logging

The `volcanite` (command line) executable is able to output several metrics and data points to so called Volcanite log files.
These log files start with one or more lines that specify the lines that Volcanite will write to append the evaluation
results to the existing log file. These lines start with the prefix `#fmt:`.
They contain placeholders that references keys for Volcanite evaluation results in the `{key}` format syntax.
You may use formatting to control decimal points, rounding, etc. as in the general [format syntax](https://fmt.dev/latest/syntax/).

The log file may contain additional lines at initialization time, serving as the header for the following results.
Just make sure that the `#fmt:` specifications are the first lines of the file as others will be ignored.
Any subsequent Volcanite calls that output results to the same log file will simply append their new results as new
lines at the end of the files, formatted with the `#fmt` strings from the start of the file.

A log file that is initialized as:
```
#fmt:{name}, {comprate_pcnt}%, {comprate_pcnt:.1}%
Evaluation Run, Compression Ratio, CR One Decimal Point
```
will look like the following after two Volcanite runs named *run_1* and *run_2* with compression ratios of *2.12345%*
and *6.6789%*:
```
#fmt:{name}, {comprate_pcnt}%, {comprate_pcnt:.1}%
Evaluation Run, Compression Ratio, CR One Decimal Point
run_1, 2.12345%, 2.1%
run_2, 6.6789%, 6.7%
```

It is possible to pass multiple log files at the same time to a Volcanite execution.
The command line arguments to specify log files and the name of an execution are `--eval-logfiles=file1[,file2,..]` and
`--eval-name=name`.

### Available Evaluation Result Keys

#### General

| Key   | Value                                                       |
|-------|-------------------------------------------------------------|
| name  | name of execution (from argument `--eval-name`)             |
| time | time stamp (*YYYY*-*MM*-*DD* *HH*:*MM*:*SS*)                |
| args | space separated command line arguments                      |

#### Compression

These results are only available if the compression is performed, i.e. no pre-computed CSGV volume is loaded. 

| Key   | Value                                                                     |
|-------|---------------------------------------------------------------------------|
| comprate | compression rate (CSGV size / original size)                              | 
| comprate_pcnt | compression rate [%]                                                      |
| comp_s | total compression time (without i/o) [s]                                  |
| comp_mainpass_s | time of compression the main pass [s]                                     |
| comp_prepass_s | time of the compression prepass [s]                                       |
| comp_gb_per_s | compression throughput [GB/s]                                             |
| csgv_gb | CSGV size [GB]                                                            | 
| orig_gb | Uncompressed volume size [GB}                                             
| orig_bytes_per_voxel | Bytes stored per voxel in uncompressed volume¹ (1\|2\|4\|8)               |
| volume_dim | Volume dimension in voxels (*WIDTH*x*HEIGHT*x*DEPTH*)                     |
| volume_labels | Number of unique labels in the volume                                     |

¹ For a fair comparison of compression rates, Volcanite assumes that the uncompressed volume stores labels in
$B = \lceil log_2(\textrm{volume_labels}) \rceil$ bytes per voxel, with B being rounded up to a power of two. 
This is even if the maximum label is higher than $2^B$.

#### Decompression
(currently not yet supported)

| Key                 | Value                                 |
|---------------------|---------------------------------------|
| decomp_cpu_gb_per_s | Decompression throughput (CPU) [GB/s] |
| decomp_cpu_s        | Decompression time (CPU) [s]          |
| decomp_gpu_gb_per_s | Decompression throughput (GPU) [GB/s] |
| decomp_gpu_s        | Decompression time (GPU) [s]          |

#### Rendering

These results are only available if an image `-i` or video `-v` is rendered. 

| Key                    | Value                                              |
|------------------------|----------------------------------------------------|
| min_spp                | minimum number of valid samples of a pixel         |
| max_spp                | maxmium number of valid samples of a pixel         |
| frame_min_ms           | minimum frame time [ms]                            |
| frame_avg_ms           | average frame time [ms]                            |
| frame_sdv_ms           | standard deviation of frame time [ms]              |
| frame_med_ms           | median frame time [ms]                             |
| frame_max_ms           | maximum frame time [ms]                            |
| frame_ms_00 to *_15    | render time of the first 16 frames [ms]            |  
| render_total_ms        | total render time for all frames [ms]              |
| rendered_frames        | number of rendered frames                          |
| mem_framebuffer_mb     | GPU memory for frame buffers [MB]                  |
| mem_uniformbuffer_mb   | GPU memory for uniform buffers [MB]                |
| mem_materials_mb       | GPU memory for attribute and material buffers [MB] |
| mem_encoding_mb        | GPU memory for the CSGV encoding [MB]              |
| mem_cache_mb           | GPU memory for the cache [MB]                      |
| mem_cache_used_mb      | occupied memory in the cache [MB]                  |
| mem_cache_fillrate     | ratio of occupied memory in the cache              |
| mem_cache_fillrate_pct | ratio of occupied memory in the cache [%]          |
| mem_emptyspace_mb      | GPU memory for empty space skipping [MB]           |
| mem_total_mb           | the total GPU memory in use [MB]                   |

## Python Script Execution

**Preliminaries:**
You need to install all Volcanite dependencies, including cmake and git.
Additionally, install python 3.12 and the python packages used by `volcaniteeval`.

The `volcaniteeval` module contains classes for executing Volcanite through system calls as well as utility functions to
handle and group command line arguments.

#### VolcaniteArg
The `VolcaniteArg` class encapsulates sets of space separated Volcanite command line arguments together with a short
identifier.
* Identifiers are concatenated using `concat_ids(..)` to identify a certain set of configuration parameters.
* Some static `VolcaniteArg` are grouped in dictionaries `args_*`, e.g. the `args_shade` dictionary contains arguments
to set all shading modes.
* Other variable `VolcaniteArg` can be obtained through `arg_*(..)` functions, e.g. the `arg_image_export(path)`
returns a `VolcaniteArg` that tells Volcanite to export the final rendered frame to `path`.

#### VolcaniteExec
The `VolcaniteExec` class encapsulates a handle to the volcanite executable through system calls.
It is configured with a `VolcaniteEvaluation` object that specifies the output directory for all evaluation results,
a git checkout and build directory, and the Volcanite log files into which all results are written. 
`checkout_and_build()` checks out the specified git commit, tag, or branch and builds the volcanite executable using cmake.
Afterward, any call to `exec(args, name)` will execute Volcanite with the given arguments and evaluation name.

### Example

```python
import volcanite.volcaniteeval as ve

# create the VolcaniteArg dictionary for all data sets
args_data = {"cells": ve.VolcaniteArg.arg_dataset("/data/cells_segmentation.raw", "cells"),
             "h01": ve.VolcaniteArg.arg_dataset("/data/h01_chunks/x{}y{}z{}.hdf5", "h01", chunks=(4,5,5)),

# setup the evaluation output directory and the log files
evaluation = ve.VolcaniteEvaluation("/volcanite-eval/my_test_eval", ve.ExistingPolicy.APPEND, "my_test_eval",
                                     [VolcaniteLogFileCfg("results.txt",
                                                          fmts=["{name},{comprate_pcnt:.3}%"],
                                                          headers=["Name,Compression Rate [%]"])],
                                    enable_log=True, dry_run=False)

# create the Volcanite executor, checkout main and build into cmake-build-release 
volcanite = ve.VolcaniteExec(evaluation, "main", "cmake-build-release")
volcanite.checkout_and_build()

# print evaluation information to console
print(volcanite.info_str())
print('\n'.join(volcanite.logs_info_str()))

# iterate over all configuration combinations and execute Volcanite
for arg_shade in VolcaniteArg.args_shading.values():
    for arg_data in args_data:
        vargs = [arg_data, arg_shade]
        name = VolcaniteArg.concat_ids(vargs)
    
        # manually log a comment line to the log file
        evaluation.get_log().log_manual("# Running evaluation: " + name)
        # execute Volcanite and passe the Volcanite log file  
        volcanite.exec(vargs, name)
        
# create a copy of the log file without comment lines that start with # which includes the #fmt: strings
evaluation.get_log().create_formatted_copy("/volcanite-eval/my_test_eval/results.csv",
                                           remove_line_prefixes=["#"])
```