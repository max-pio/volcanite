# Usage

Volcanite can be used as an interactive GUI application or through its command line interface.
The latter is especially useful for automating tasks or rendering images on remote systems.

## Supported Segmentation Volume File Formats

See the [Python](Python.md) readme and [converter.py](../python/volcanite/src/volcanite/converter.py) for converting
file types that are not listed here into Volcanite compatible formats using Python.
If you have no segmentation volume at hand, you can either have a look at the [Example Data](ExampleData.md) or let Volcanite create a synthetic volume with `./volcanite +synth`.  

* **Volcanite RAW**
  A simple binary file with the following format:
  One `\n` terminated utf8 line specifying the integer data type, followed by one line containing space separated width, height, depth.
  Followed by the raw binary data.
  File name must end with `.vraw`.
  Example for a volume with 128 voxels in the X, 256 voxels in the Y, and 400 voxels in the Z dimension:
```
uint32
128 256 400
[BINARY VOXEL DATA IN LITTLE-ENDIAN C-ORDER: x0y0z0 x1x0y0 ...]
```

* **NRRD**
  An NRRD file following the [NRRD0004 format](https://teem.sourceforge.net/nrrd/format.html).
  File name must end with `.nrrd` or `.nhdr`.
  Detached headers are supported.

* **VTI**
  The XML image file format of the Visualization Toolkit (VTK).
  File name must end with `.vti`.
  If the VTK libraries at version 9.3.1 or higher are not available, only a simple subset of these image data files can be loaded that must have the format:
```
<VTKFile type="ImageData" version=[*] byte_order=[BYTE_ORDER] header_type="UInt64">
    <ImageData WholeExtent="0 [WIDTH] 0 [HEIGHT] 0 [DEPTH]" Origin="0 0 0" Spacing="1.000000e+00 1.000000e+00 1.000000e+00">
        <CellData Scalars="[*]">
            <DataArray type="UInt32" Name="[*]" format="appended" offset="0" NumberOfComponents="1"/>
        </CellData>
    </ImageData>
    <AppendedData encoding="raw">
        [BINARY VOXEL DATA]
    </AppendedData>
</VTKFile>
```

* **HDF5**
  An HDF5 file whose first object must be a dataset containing the integer voxel data in the correct shape.
  This format is only available when the hdf5 library is available:
  Either by installing the package `libhdf5-dev` on Ubuntu or by using the [precompiled binary distributions](https://www.hdfgroup.org/downloads/hdf5/) from the HDF group.
  File name must end with `.hdf5` or `.h5`.

## Application

![Renderer Preview Image](volcanite_app.jpg)

### GUI Parameters

**General** allows to set the *Voxel Size* of the data set and clip the volume along axis aligned *Splitting Planes*.
The *Rendering Preset* drop down menu allows quickly switching between default rendering configurations.
Below you can export and import the current rendering configuration and take screenshots.
In addition to being able to *Flip* certain axes, *Axis Order* allows to transpose the data set coordinate system.

**Rendering** provides control over all rendering and shading parameters.
*Constant Color* allows to blend between the shaded render image and a constant color mapping of voxel labels without any shading.
*Global Illumination* enables shadow rays and path tracing, with the *Directional <> Ambient* slicer controlling
the weighting between shadowing rays and indirect illumination.
The directional light source for the shadow rays can be controlled with the *Light Direction* widget.
If *Environment Map* is enabled, path tracing uses a more natural environment light instead of a constant white illumination.
If ambient lighting is computed, it is advisable to enable *Denoising*.
Last, the final output image can be tonemapped, as well as contrast, brightness, or gamma corrected.

**Display** contains the *Background Color* gradient settings, and allows to resize the application window.
The number set for *Accumulation Frames* determines how many frames are rendered until the rendering accumulation stops.
This is mostly relevant for renderings using path tracing, where a converged rendering is only achieved after multiple frames.
A value of 0 accumulates frames indefinitely.
If render times are too slow or GPU memory is insufficient, the *Performance Optmization* tab provides helpful options. 
The *Resolution Subsampling* parameter can be increased to render less than one sample per pixel per frame.
Note that the final output resolution does not change, but the image needs to be progressively rendered over multiple frames:
A value of 0 uses no subsampling, a value of 1 computes 1/4th of the pixels per frame, a value of 2 computes 1/16th of the pixels etc.
Set the *Accumulation Frames* parameter to at least 4, 8, .. respectively to obtain correct images.
*Decompression Path Length* determines up to which depth indirect ray bounces (at most *Path Length*) will decompress bricks.
Lower values of 0 or 1 will significantly reduce memory consumption but will introduce a (usually small) bias to shadows and indirect illumination.
The *Decompression LOD Bias* will decompress bricks in a finer (higher values) or coarser (lower values) resolution than usual.
Lower values significantly improve performance and reduce memory consumption but will render the volume in a visibly coarser resolution.

**Materials** is the material editor that allows to group labels or label attributes into different groups.
If no attribute data base was specified on Volcanite startup using the `-a` command line argument
(see [Command Line Interface](#command-line-interface)), only the voxel labels directly can be used as virtual attributes.
All voxels whose *Filter* attribute falls into the specified interval are assigned to a given material.
For these voxels, another *Attribute* can be specified to be displayed with a color map:
The given interval is mapped to color values, either from a *Precomputed Colormap*, between two
border colors of a *Divergent Colormap*, or to a single *Constant Color*.
Color maps can further be imported from PNG files where the center pixel row of the PNG image, left to right, is treated as the color map.
In *Clamp* mode, all attribute values outside the interval are mapped to the edge values while in *Wrap* mode, the same
color map is repeated successively, and *Random* assigns labels randomly to colors from the color map.
The *Opacity* slider allows to display the material as semi-transparent, while the *Emission* slider controls how much
light the voxels of this material emit by themselves.


### Keyboard and Mouse Controls

Press and hold a mouse button to rotate the camera around the camera focus position. `SHIFT` and `CTRL` lock the rotation to a single axis.
Move the camera focus position with the `W` `A` `S` `D` and `Q` `E` keys with `SHIFT` increasing and `CTRL` decreasing the speed.
Pressing `R` performs a constant rotation around the y-axis. `CTRL` + `R` 

The number keys [`0`-`9`] provide quick access to rendering configurations:
Pressing `CTRL` + [`0`-`9`] stores the current setup while pressing one of [`0`-`9`] loads a previously stored configuration.

Hitting `F9` starts recording the camera pose and frame time of frame until it is pressed again.
Both resulting output files are stored in a subfolder `volcanite_video`.
The record can be replayed by hitting `F10`.
`F11` replays the record and outputs a PNG image for each frame that can later be concatenated to a video using an external program like ffmpeg, possibly including the frame time log:
```
ffmpeg -f concat -safe 0 -i ./volcanite_video/video_timing.txt ./volcanite_video/video.mp4
```

`ESC`closes the application which is useful if Volcanite is executed with the `--fullscreen` command line option.
If you are developing your own renderers, hitting `F5` is useful to recompile all currently used shaders.

## Command Line Interface

To force selecting a certain Vulkan device for rendering, you can set the environment variable `VOLCANITE_DEVICE` to
a requested index. Otherwise, Volcanite will select an appropriate device, usually the dedicated GPU in your system.
The categorized command line arguments are explained below.
See `./volcanite --help` for the most recent list of command line arguments.
Some arguments like chunked input volume names (`--chunked`), video output frames (`-v`),
or evaluation log formatting templates (`--eval-logfile`) contain templated placeholders in braces `{}`.
These arguments usually follow the C++ [fmt formatting principles](https://hackingcpp.com/cpp/libs/fmt.html), e.g. specifying `-v out_{:4}.jpg`
will export video frames to files `out_0000.jpg`, `out_0001.jpg` and so forth.  
The general usage of Volcanite is

```
 volcanite  [-h] [--version] [--headless] [--threads <int>]             
            [-c <file>] [-b <8|16|32|64|128>] [-s <0|1|2>] [-o <(a|o|p|n|x|y|z|l|d[-]|s)*>]
                        [--freq-sampling <int>] [--chunked <xn,yn,zn>] [--relabel]
                        [-a <database.sqlite[,table[,label]] or database.csv[,label[,separator]]>] 
            [-d <file>]
            [-i <file>]  [-v <formatted file>] [--record-frames <int>] [--record-in <file>]
            [--fullscreen] [--no-vsync] [-r <[Width]x[Height]>] [--config <config files or strings>]
            [--cache-size <size>] [--cache-palette] [--stream-lod] 
            [--eval-logfiles <file>] [--eval-name <string>] [--eval-print-keys] [--stats] [-t]
            [--random-access] [--decode-sm] [--cache-mode <n|v|b>] [--empty-space-res <0|1|2|..|32|64>]
            [--verbose] [--dev] [--shader-def <string>]
            [input volume]
```

#### General 

* `-h,  --help`

  Displays usage information and exits.

* `--version`
 
  Displays version information and exits.

* `--headless`

  Do not start the GUI application. You must specify an `[input volume]` in this case. 

* `--threads <int>`

  Number of CPU threads that will be used for parallelization on CPU host.

* `--, --ignore_rest`

  Ignores the rest of the labeled arguments following this flag.


* `input <(<volume file>|+synth[_args*])>`

  Either a previously compressed .csgv file to render, or a segmentation
  volume file to compress or render. +synth to create and process a synthetic volume.


#### Compression

* `-c <file>,  --compress <file>`

  Export the compressed volume to the given csgv file and any attribute database along with it. 
  By default, a compressed .csgv volume is exported to the `[input volume]` file location.

* `-b <8|16|32|64|128>,  --brick-size <8|16|32|64|128>`

  Compress with given brick size.

* `-s <0|1|2>,  --strength <0|1|2>`

  Compress with more expensive but stronger variable bit-length encoding (1). Use two frequency tables for even stronger compression (2).


* `-o <(a|o|p|n|x|y|z|l|d[-]|s)*>,  --operations <(a|o|p|n|x|y|z|l|d[-]|s)*>`

  Select the CSGV operations used in the encoding during compression.
  Must be a string of one or more of the following symbosl:
  [p]arent, all [n]eighbors / [x,y,z] neighbor, palette [l]ast, palette [d]elta, [s]top bits.
  Placeholders: [a]ll or [o]ptimized. Use [d-] instead of [d] for the legacy palette delta operation. 

* `--chunked <xn,yn,zn>`

  Compress a chunked segmented volume using a string formatted `[input volume]` path with `{}` as chunk index placeholders.
  The chunk index range uses inclusive x, y, and z indices as: `.*{[0..<xn>]}.*{[0..<yn>]}.*{[0..<zn>]}.*`.
  E.g. the input `volume_x{}_y{}_z{}.hdf5` with `--chunked 0 1 1` will process 4 chunks from
 `volume_x0_y0_z0.hdf5` to `volume_x0_y1_z1.hdf5`. Inner chunks must have a common volume dimension that is a multiple
  of the brick size `b` which is usually 32.

* `--freq-sampling <int>`

  Accelerates the compression pre-pass by the given factor cubed. Affects `--strength` 1 or 2 only.

* `--relabel`
  Relabel the voxels so that all occurring label values span a continuous domain from 0 to max_label-1
  even if no attribute database is used.


* `-a <database.sqlite[,table[,label]], --attribute <database.sqlite[,table[,label]]` or

  `-a database.csv[,label[,separator]]>, --attribute database.csv[,label[,separator]]>`

  SQLite or CSV attribute database: `{file.sqlite}[,{table/view name}[,
  {label column referenced in volume}]]` or `{file.csv}[,{label column
  referenced in volume}[,{csv separator}]]`. The given file must contain one row for each label in the volume.
  The volume voxels will be relabelled (see `--relabel`) and the attribute values will be exposed in Volcanite's
  material editor during rendering.

* `-d <file>,  --decompress <file>`

  Decompressed the volume and export it to the given .vraw, .hdf5, or .nrrd file.


#### Image and Video Export

* `-i <file>,  --image <file>`

  Renders an image to the given file on startup.

* `-v <formatted file>,  --video <formatted file>`

  Video output with one image output file per frame. The formatted file path must contain a single {} placeholder
  which will be replaced with frame index. Example: ./out{:04}.jpg

* `--record-in <file>`

  File that stores a previously exported camera path for replay on startup. Must be used with `-i` or `-v`.

* `--record-frames <int>`

  How many render frames are accumulated per output frame, or viewpoint respectively, of a camera path.
  Must be used with `--record-in` or `-v`.

#### Rendering

* `--fullscreen`

  Start the renderer in fullscreen mode.

* `--no-vsync`

  Disable vertical synchronization in renderer.

* `-r <[Width]x[Height]>,  --resolution <[Width]x[Height]>`

  Startup render resolution as `[Width]`x`[Height]` in pixels.


* `--config <{(.vcfg file | rendering preset | string);}*>`

  Imports startup rendering parameters from a list of .vcfg files or direct configuration strings, separated by ';'.
  Possible rendering presets are `local-shading`, `global-shadows`, `ambient-occlusion`, and `path-tracing`.
  Direct configuration strings follow the format: `[{GUI window}] {parameter label}: {parameter value(s)}`.
  Example: `--config global-shadows;../my_config.vcfg;"[Display] Resolution Subsampling: 1"`

* `--cache-size <size>`

  Size in MB of the renderer's brick cache. Set to 0 to allocate all available GPU memory (up to 4 GB).

* `--cache-palette`

  Stores packed palette indices in the GPU brick cache instead of full 32 bit labels.
  Use this option if the GPU brick cache is not big enough to fit all visible volume regions.
  This works best if the CSGV volume was encoded with the palette delta operation, i.e. `-o a` or `-o [.*]d`.

* `--stream-lod`

  Streams the finest level of detail of brick encodings to GPU on demand. Helps if GPU memory is a limiting factor.


#### Evaluation

* `--eval-logfiles <file>`

  Comma separated list of files into which evaluation results will be appended.
  The evaluation files must contain at least one line prefixed with `#fmt:` specifying the formatting template string from
  which the new evaluation result text line will be created. See `--eval-print-keys` for all available replacement keys.
  For example, for an evaluation file containing the formatting line `#fmt:{name}, compression rate: {comprate_pcnt:.2}%`,
  Volcanite could append the result line `my-evaluation, compression rate: 3.14%` after execution finished.  

* `--eval-name <string>`
  
  Title of this evaluation which will be available in log files as `{name}`. Must be used with --eval-logfile.

* `--eval-print-keys`

  Print all available evaluation keys to the console and exit.

* `--stats`

  Exports statistics to a CSV file next to the .csgv output file after performing the compression.

* `-t,  --test`

  Test the compressed volume by comparing it to the input volume after performing the compression.

#### Random Access Compression (CSGV-R)

The following arguments will provide alternative random access encodings as in
*Piochowiak, Kurpicz, and Dachsbacher (2025) Random Access Segmentation Volume Compression for Interactive Visualization. Proc. EuroVis 25.* 

* `--random-access`
  Encode in a format that supports random access and in-brick parallelism for the decompression.

* `--cache-mode <n|v|b>`

  Content in the cache: [n] no cache [v] single voxels [b] full bricks (default).
  Modes except [b] must be used with `--random-access`.

* `--decode-sm`

  Copy brick encodings to shared memory before decoding. Must be used with `--random-access`.

* `--empty-space-res <0|1|2|4|8|16|32|64>`

  Groups n³ voxels into one empty space entry. Requires cache-mode v. Set 0 to disable empty space skipping.
  Must be used with `--random-access`. 


#### Development

* `-v,  --verbose`
  Enables verbose debug output.

* `--dev`
  Reveals all development render parameters in the application GUI.

* `--shader-def <string>`

  String of ; separated definitions that will be passed on to the  shader. e.g. `MY_VAL=64;MY_DEF`. Use with care.


### Headless Mode

Volcanite can be run in `--headless` mode to not open the application window.
This allows executing the renderer on systems without a windowing system or for calling Volcanite from scripts.

If your system does not provide a GUI or the `xorg-dev` library, you can pass the CMake option `HEADLESS` to the build
pipeline to not include any windowing functionality in Volcanite altogether
(see [Setup.md](Setup.md#headless-builds)).
In this case, you will only be able to run Volcanite with `--headless` set.

Useful command line arguments to be used in combination with `--headless` are `-i <file>` to render the input volume to a given
image file, or `-c <file>` and `-d <file>` to compress or decompress a volume into a given file location.

### Examples
* `./volcanite volume`
  Starts the Volcanite renderer for the given volume.
* `./volcanite --headless -r 1920x1080 -i screenshot.png volume.vti`
  Exports a render image without starting the application.
* `./volcanite --headless -b 64 -s 2 -c out.csgv volume.vti`
  Exports a strongly compressed volume.
  [//]: # (* `./volcanite --headless -d out.vti volume.csgv`)
  [//]: # (  Decompresses volume.csgv to out.vti.)
* `./volcanite --config local-shading --cache-size 512 -b 16 --freq-sampling 8 --stream-lod -s 0 volume.vti`
  Starts Volcanite for limited GPU capabilities and small volumes.
* `./volcanite --cache-size 0 --stream-lod --cache-palette --palette-cache large_volume.csgv`
  Efficiently use as much GPU memory as possible to visualize a large volume.
* `./volcanite --headless -c out.csgv --chunked 1,3,0 vol_x{}_y{]_z{}.vti`
  Compresses chunked volume with 8 chunks from vol_x0_y0_z0.vti to vol_x1_y3_z0.vti without starting the application.