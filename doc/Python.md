# Usage with Python

Currently, directly using Volcanite through python is not supported, but python bindings are planned for future releases.
The only way of visualizing python data (e.g. from numpy arrays) with Volcanite is to export the data to a file from which Volcanite can read it.
The [converter.py](../projects/volcanite/python/converter.py) file offers import and export for multiple supported file formats.

Voxel byte arrays are in little endian C-order meaning that the X dimension of the array is contiguous in memory.
For a numpy array, the first index of the volume's shape is denoting the Z dimension of the volume.

To export a numpy segmentation volume `volume` to an NRRD file, use the following code:

```python
import numpy as np

def export_to_nrrd(volume, path_prefix):
    with open(path_prefix + ".nrrd", "wb") as file:
        # write header:
        file.write("NRRD0004\n".encode('utf8'))
        file.write("type: uint32\n".encode('utf8'))
        file.write("dimension: 3\n".encode('utf8'))
        file.write("space: left-posterior-superior\n".encode('utf8'))
        file.write("kinds: domain domain domain\n".encode('utf8'))
        file.write(("sizes: " + str(volume.shape[2]) + " " + str(volume.shape[1]) + " " + str(volume.shape[0]) + "\n").encode('utf8'))
        file.write("endian: little\n".encode('utf8'))
        file.write("encoding: raw\n".encode('utf8'))
        file.write("\n".encode('utf8'))
        # write binary payload in c-order
        np.ascontiguousarray(volume.astype('uint32')).tofile(file)
```

The following example code exports a volume to a simplified Volcanite raw format:

```python
import numpy as np

def export_to_vraw(volume, path_prefix):
    with open(path_prefix + ".vraw", "wb") as file:
        # write header: two \n terminated utf8 lines as:
        # [VOXEL_DIMENSION_X] [VOXEL_DIMENSION_Y] [VOXEL_DIMENSION_Z]\n
        # uint32\n
        file.write(
            (str(volume.shape[2]) + " " + str(volume.shape[1]) + " " + str(volume.shape[0]) + "\n").encode('utf8'))
        file.write("uint32\n".encode('utf8'))
        # write binary array (C-order, 4 byte per unsigned uint voxel)
        np.ascontiguousarray(volume.astype('uint32')).tofile(file)
```

Import files of this format with:

```python
def read_from_vraw(vraw_path):
    with open(vraw_path, "rb") as file:
        # read header
        shape_str = file.readline()[:-1].decode('utf8').split()
        type = file.readline()[:-1].decode('utf8')
        # read binary payload
        volume = np.fromfile(file, dtype=type)
        volume = volume.reshape([int(shape_str[2]), int(shape_str[1]), int(shape_str[0])])
    return volume
```

To split a large volume into smaller chunk files, use the following code.
The chunk size must be a multiple of the brick size (usually 32) that will be used by Volcanite later:

```python
def export_chunk_split_vraw(volume, path_prefix, chunk_size):
    for z in range(0, volume.shape[0], chunk_size[0]):
        for y in range(0, volume.shape[1], chunk_size[1]):
            for x in range(0, volume.shape[2], chunk_size[2]):
                export_to_vraw(volume[z:(min(volume.shape[0], z+chunk_size[0])),
                                      y:(min(volume.shape[1], y+chunk_size[1])),
                                      x:(min(volume.shape[2], x+chunk_size[2]))],
                               path_prefix + "_x" + str(x//chunk_size[2])
                                           + "_y" + str(y//chunk_size[1])
                                           + "_z" + str(z//chunk_size[0]) + ".vraw")
```