# Usage

Volcanite can be used as an interactive GUI application or through its command line interface.
The latter is especially useful for automating tasks or rendering images on remote systems.

## Supported Segmentation Volume File Formats

See the [Python](Python.md) readme and [converter.py](../projects/volcanite/python/converter.py) for converting
file types that are not listed here into Volcanite compatible formats using Python.

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
  If the VTK libraries are not available, only a simple subset of these image data files can be loaded that must have the format:
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
Below you can export and import the current rendering configuration and take screenshots.

**Rendering** provides control over all rendering and shading parameters.
*Constant Color* allows to blend between the shaded render image and a constant color mapping of voxel labels.
*Global Illumination* enables shadow rays and path tracing, with the *Direct Light / Path Tracing* slicer controlling
the weighting between the two.
The directional light source for the shadow rays can be controlled with the *Light Direction* widget.
If *Environment Map* is enabled, path tracing uses a more natural environment light instead of a constant white illumination.

**Display** contains the *Background Color* gradient settings, and allows to resize the application window.
The number set for *Accumulation Frames* determines how many frames are rendered until the rendering stops.
This is mostly relevant for renderings using path tracing, where a converged rendering is only achieved after multiple frames.
A value of 0 accumulates frames indefinitely.
If render times are too slow, *Subsampling Resolution* parameter can be increased to render less than one sample per pixel per frame.
Note that the final output resolution does not change, but the image needs to be progressively rendered over multiple frames:
A value of 0 uses no subsampling, a value of 1 computes 1/4th of the pixels per frame, a value of 2 computes 1/8th of the pixels etc.
Set the *Accumulation Frames* parameter to at least 4, 8, .. respectively to obtain correct images.

**Materials** is the material editor that allows to group labels or label attributes into different groups.
If no attribute data base was specified on Volcanite startup using the `-a` command line argument
(see [Command Line Interface](#command-line-interface)), only the voxel labels can be used as virtual attributes.
All voxels whose *Discriminator Attribute* falls into the specified interval are assigned to a given material.
For these voxels, another *Attribute* can be specified to be displayed with a color map:
The given interval is mapped to color values, either from a *Precomputed Colormap*, between two
border colors of a *Divergent Colormap*, or to a single *Constant Color*
In *Clamp* mode, all attribute values outside the interval are mapped to the edge values while in *Wrap* mode, the same
color map is repeated successively.
The *Opacity* slider allows to display the material as semi-transparent, while the *Emission* slider controls how much
light the voxels of this material emit by themselves.




### Keyboard and Mouse Controls

Press and hold a mouse button to rotate the camera around the camera focus position. `SHIFT` and `CTRL` lock the rotation to a single axis.
Move the camera focus position with the `W` `A` `S` `D` + `Q` `E` keys.
Pressing `R` performs a constant rotation around the y-axis.

Hitting `F9` starts recording the camera pose and frame time of frame until it is pressed again.
Both resulting output files are stored in the user home directory in a subfolder `vvv_video`.
The record can be replayed by hitting `F10`.
`F11` replays the record and outputs a PNG image for each frame that can later be concatenated to a video using an external program like ffmpeg:
```
ffmpeg -f concat -safe 0 -i ~/vvv_video/video_timing.txt ~/vvv_video/video.mp4
```

## Command Line Interface

The categorized command line arguments are explained below.
The general usage of Volcanite is

```
volcanite [-h] [--version] [--headless] [--threads <int>] 
          [-c <file>] [-d <file>]
          [-a <database[,table[,label]]>] [--relabel]
          [-b <8|16|32|64|128>] [-s <0|1|2>] [--chunked <xn,yn,zn>] [--freq-sampling <int>]
          [--config <file>] [-r <file>] [-i <file>]
          [--stream-lod] [--cache-palette] [--cache-size <size>]
          [--t] [--stats] [--dev]
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
  Number of CPU threads that will be used for parallelization.

* `input <volume>`
  Either a previously compressed .csgv file to render or a segmented volume to compress and / or render.

#### Compression

* `-c <file>,  --compress <file>`
  Export the compressed volume to the given csgv file and any attribute database along with it. 
  By default, a compressed .csgv volume is exported to the `[input volume]` file location.    

* `-d <file>,  --decompress <file>`
  Export the decompressed volume to given file. *(not yet supported)*

* `-a <database[,table[,label]]>,  --attribute <database[,table[,label]]>`
  An SQLite database storing attributes per label in the volume as: `{database filepath}[,{attribute
  table/view name}[,{label column name that is referenced by volume labels}]]`. The volume voxels will be relabelled
  (see `--relabel`) and the attribute values will be exposed in Volcanite's material editor.

* `--relabel`
  Relabel the voxels so that all occurring label values span a continuous domain even if no attribute database is used.

* `--chunked <xn,yn,zn>`
  Compress a chunked segmented volume using a string formatted `[input volume]` path with `{}` as chunk index placeholders.
  The chunk index range uses inclusive x, y, and z indices as: `.*{[0..<xn>]}.*{[0..<yn>]}.*{[0..<zn>]}.*`.
  E.g. the input `volume_x{}_y{}_z{}.hdf5` with `--chunked 0 1 1` will process 4 chunks from
 `volume_x0_y0_z0.hdf5` to `volume_x0_y1_z1.hdf5`. Inner chunks must have a common volume dimension that is a multiple
  of the brick size `b` which is usually 32.

* `--freq-sampling <int>`
  Accelerates the compression pre-pass by the given factor cubed. Affects `--strength` 1 or 2 only.

* `-s <0|1|2>,  --strength <0|1|2>`
  Use CSGV compression variable bit-length encoding (1). Use two frequency tables for even stronger compression (2).

* `-b <8|16|32|64|128>,  --brick-size <8|16|32|64|128>`
  CSGV Compression brick size.

#### Rendering

* `--cache-size <size>`
  Size in MB of the renderer's brick cache. Set to 0 to allocate all available GPU memory (up to 4 GB).

* `--cache-palette`
  Stores palette indices in brick cache instead of labels. Use this option if the GPU brick cache is not big enough to
  fit all visible volume regions.

* `--stream-lod`
  Streams the finest level of detail of brick encodings to GPU on demand. Helps if GPU memory is a limiting factor.

* `-i <file>,  --image <file>`
  Renders an image to the given file on startup. This is the only way to render a data set in `--headless` mode.

* `-r <file>,  --resolution <file>`
  Specifies startup render resolution as `{Width}x{Height}`.

* `--config <file>`
  Imports startup renderer parameters from the given config file.

#### Development

* `-v,  --verbose`
  Enables verbose debug output.

* `-t,  --test`
  Runs tests after performing the compression.

* `--stats`
  Exports statistics to a CSV file next to the csgv output file after performing the compression.

* `--dev`
  Reveals all development render parameters in the application GUI.

### Headless

Volcanite can be run in `--headless` mode to not open the application window.
This allows executing the renderer on systems without a windowing system or for calling Volcanite from scripts.

If your system does not provide a GUI or the `xorg-dev` library, you can pass the CMake option `HEADLESS` to the build
pipeline to not include any windowing functionality in Volcanite altogether
(see [Setup.md](Setup.md#headless-builds)).
In this case, you will only be able to run Volcanite with `--headless` set.

Useful command line arguments to be used in combination with `--headless` are `-i` to render the input volume to a given
image file, or `-c` to compress the input volume to a .csgv file.

### Examples
* `./volcanite volume`
  Starts the Volcanite renderer for the given volume.
* `./volcanite --headless -r 1920x1080 -i screenshot.png volume.vti`
  Exports a render image without starting the application.
* `./volcanite --headless -b 64 -s 2 -c out.csgv volume.vti`
  Exports a strongly compressed volume.
  [//]: # (* `./volcanite --headless -d out.vti volume.csgv`)
  [//]: # (  Decompresses volume.csgv to out.vti.)
* `./volcanite --config low.cfg --cache-size 512 -b 16 -s 0 volume.vti`
  Starts Volcanite for limited GPU capabilities and small volumes.
* `./volcanite --cache-size 0 --stream-lod --palette-cache large_volume.csgv`
  Efficiently use as much GPU memory as possible to visualize a large volume.
* `./volcanite --headless -c out.csgv --chunked 1,3,0 vol_x{}_y{]_z{}.vti`
  Compresses chunked volume with 8 chunks from vol_x0_y0_z0.vti to vol_x1_y3_z0.vti without starting the application.