# Usage

## Command Line Arguments

### Headless

## Application 

### GUI Parameters

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

