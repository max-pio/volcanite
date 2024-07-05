#  Copyright (C) 2024, Max Piochowiak, Karlsruhe Institute of Technology
#
#  This program is free software: you can redistribute it and/or modify
#  it under the terms of the GNU General Public License as published by
#  the Free Software Foundation, either version 3 of the License, or
#  (at your option) any later version.
#
#  This program is distributed in the hope that it will be useful,
#  but WITHOUT ANY WARRANTY; without even the implied warranty of
#  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
#  GNU General Public License for more details.
#
#  You should have received a copy of the GNU General Public License
#  along with this program.  If not, see <https://www.gnu.org/licenses/>.

import io
import os
import string

import numpy as np
import pyvista as pv
import h5py
import PIL.Image as Image
import nibabel as nib

import matplotlib.pyplot as plt


########################################################################################################################
#                                            READER / WRITER PER FORMAT                                                #
########################################################################################################################

def get_format_key_count(formatted_string):
    """:return: the number of python string format keys in formatted_string."""
    return len([f for f in string.Formatter().parse(formatted_string) if f[2] is not None])


# vraw (Volcanite simplified raw format)
def read_vraw(path_in):
    with open(path_in, "rb") as file:
        # read header
        shape_str = file.readline()[:-1].decode('utf8').split()
        type = file.readline()[:-1].decode('utf8')
        # read binary payload
        vraw_volume = np.fromfile(file, dtype=type)
        vraw_volume = vraw_volume.reshape([int(shape_str[2]), int(shape_str[1]), int(shape_str[0])])
    return vraw_volume


def write_vraw(volume, out_path):
    file = open(out_path, "wb")
    # write two line header:
    # [DimX] [DimY] [DimZ]
    # [data type]
    file.write(
        (str(volume.shape[2]) + " " + str(volume.shape[1]) + " " + str(volume.shape[0]) + "\n").encode('utf8'))
    file.write("uint32\n".encode('utf8'))
    # write binary
    np.ascontiguousarray(volume.astype('uint32')).tofile(file)
    # for z in range(volume.shape[0]):
    #     for y in range(volume.shape[1]):
    #         for x in range(volume.shape[2]):
    #             file.write(volume[z, y, x])
    file.close()


# NRRD4
def read_nrrd(path_in):
    raise NotImplementedError("reading NRRD files not yet implemented")


def write_nrrd(volume, out_path):
    with open(out_path, "wb") as file:
        # write header:
        file.write("NRRD0004\n".encode('utf8'))
        file.write("type: uint32\n".encode('utf8'))
        file.write("dimension: 3\n".encode('utf8'))
        file.write("space: left-posterior-superior\n".encode('utf8'))
        file.write("kinds: domain domain domain\n".encode('utf8'))
        file.write(
            ("sizes: " + str(volume.shape[2]) + " " + str(volume.shape[1]) + " " + str(volume.shape[0]) + "\n").encode(
                'utf8'))
        file.write("endian: little\n".encode('utf8'))
        file.write("encoding: raw\n".encode('utf8'))
        file.write("\n".encode('utf8'))
        # write binary payload in c-order
        np.ascontiguousarray(volume.astype('uint32')).tofile(file)


# HDF5
def read_hdf5(path_in):
    f = h5py.File(path_in, 'r')
    key = list(f.keys())[0]
    print("read hdf5 file, using key " + key + ". Has shape " + str(f[key].shape) + ", type " + str(f[key].dtype))
    return f[key][()]


def write_hdf5(volume, path_out):
    with h5py.File(path_out, "w") as f:
        f.create_dataset("data", shape=volume.shape, dtype='uint32', data=volume.data, compression="gzip")


# Sliced TIFF
def read_sliced_tiff(path_in_format, slice_axis=0):
    """read the volume from tiff slices with the path string. path_format must use python3 format to insert integer
    slice ids, e.g. 'my_volume_{}.tiff' will write files my_volume_0.tiff, my_volume_1.tiff ..."""

    if get_format_key_count(path_in_format) != 1:
        raise Exception("File path must contain exactly 1 python string format key")

    # find slice count
    slice_count = 0
    while os.path.exists(path_in_format.format(slice_count)):
        slice_count += 1

    if slice_count == 0:
        raise Exception("Path " + path_in_format + " does not yield any files")

    # read the first slice
    slice_list = []
    for s in range(slice_count):
        slice_list.append(np.expand_dims(np.asarray(Image.open(path_in_format.format(s)), dtype='uint32'), slice_axis))
    return np.concatenate(slice_list, axis=slice_axis)


def write_sliced_tiff(volume, path_out_format):
    raise NotImplementedError("writing sliced TIFF files not yet implemented")


# Sliced PNG
def read_sliced_png(path_in_format):
    raise NotImplementedError("reading sliced PNG files not yet implemented")

def write_sliced_png(volume, path_out_format):
    """Write the volume as 2D RGBA8 PNG files slices along the z-axis. Each of the RGBA channels stores 8 bits of the
     32 bit volume labels. The least significant 8 bits are stored in the red channel. path_format must use python3
     format to insert integer slice ids, e.g. 'my_volume_{}.png' will write files my_volume_0.png, my_volume_1.png ...
     """

    if get_format_key_count(path_out_format) != 1:
        raise Exception("File path must contain exactly 1 python string format key")

    #    for z in tqdm(range(labels.shape[0])):
    for z in range(volume.shape[0]):
        slice = np.stack([volume[z] % 256, (volume[z] / 256) % 256, (volume[z] / (256 * 256)) % 256,
                          (volume[z] / (256 * 256 * 256) % 256)], axis=-1)
        image = Image.fromarray(slice.astype('uint8'))
        image.save(path_out_format.format(z), 'png', compress_level=9)

# numpy
def read_numpy(path_in):
    """Reads a volume from npy or npz numpy files. For npz files, returns the first numpy array of the archive."""
    if path_in.endswith(".npz"):
        volume_archive = np.load(path_in)
        return next(iter(volume_archive))
    else:
        return np.load(path_in)

def write_numpy(volume, path_out, compressed = True):
    if compressed:
        volume.savez(path_out)
    else:
        volume.save(path_out)

# nifti
def read_nifti(path_in):
    return np.array(nib.load(path_in).dataobj)

def write_nifti(volume, path_out):
    raise NotImplementedError("writing nifti files not yet implemented")

# VTI
def read_vti(path_in):
    vti = pv.read(path_in)
    return vti.active_scalars.reshape(np.array(vti.dimensiopns) - 1)

def write_vti(volume, path_out):
    raise NotImplementedError("writing vti files not yet implemented")

########################################################################################################################
#                                               UTILITY FUNCTIONS                                                      #
########################################################################################################################

def write_volume(volume, path_out):
    """Automatically selects the writer for the respective format based on path_out file extension."""
    extension = path_out[(path_out.rfind('.') + 1):].lower()
    if extension == "vraw" or extension == "raw":
        write_vraw(volume, path_out)
    elif extension == "nrrd":
        write_nrrd(volume, path_out)
    elif extension == "hdf5" or extension == "h5":
        write_hdf5(volume, path_out)
    elif extension == "tiff":
        write_sliced_tiff(volume, path_out)
    elif extension == "png":
        write_sliced_png(volume, path_out)
    elif extension == "np":
        write_numpy(volume, path_out, False)
    elif extension == "npz":
        write_numpy(volume, path_out, True)
    elif path_out.endswith(".nii.gz"):
        return write_nifti(volume, path_out)
    elif extension == "vti":
        return write_vti(volume, path_out)
    else:
        raise Exception("unknown segmentation volume file extension " + extension)

def read_volume(path_in):
    """Automatically selects the reader for the respective format based on path_out file extension."""
    extension = path_in[(path_in.rfind('.') + 1):].lower()
    if extension == "vraw" or extension == "raw":
        return read_vraw(path_in)
    elif extension == "nrrd":
        return read_nrrd(path_in)
    elif extension == "hdf5" or extension == "h5":
        return read_hdf5(path_in)
    elif extension == "tiff":
        return read_sliced_tiff(path_in)
    elif extension == "png":
        return read_sliced_png(path_in)
    elif path_in.endswith(".nii.gz"):
        return read_nifti(path_in)
    elif extension == "vti":
        return read_vti(path_in)
    else:
        raise Exception("unknown segmentation volume file extension " + extension)


#  chunked export
def write_chunked_volume(volume, path_out_format, chunk_size):
    """Exports the volume to a set of files where each file is a volume chunk with dimensions chunk_size^3. The file
    output format is selected based on the file extension of path_out_format. path_out_format must contain exactly
     three python string format keys that will be replaced with x y z chunk indices. e.g. 'my_volume_x{}y{}z{}.raw'."""
    if get_format_key_count(path_out_format) != 1:
        raise Exception("File path must contain exactly 1 python string format key")

    for z in range(0, volume.shape[0], chunk_size[0]):
        for y in range(0, volume.shape[1], chunk_size[1]):
            for x in range(0, volume.shape[2], chunk_size[2]):
                print("Writing " + path_out_format.format(x // chunk_size[2], y // chunk_size[1], z // chunk_size[0]))
                write_volume(volume[z:(min(volume.shape[0], z + chunk_size[0])),
                             y:(min(volume.shape[1], y + chunk_size[1])),
                             x:(min(volume.shape[2], x + chunk_size[2]))],
                             path_out_format.format(x // chunk_size[2], y // chunk_size[1], z // chunk_size[0]))

def read_chunked_volume(in_path_prefix):
    raise NotImplementedError("reading chunked volumes is not yet implemented")

def convert(path_in, path_out):
    write_volume(read_volume(path_in), path_out)

def test_vis(volume, row_count=2, col_count=3, colormap='turbo'):
    """Plot (row_count * col_count) 2D slices of the segmentation volume."""
    print("Plotting volume with shape " + str(volume.shape) + " and max. value " + str(np.max(volume)))
    fig, axs = plt.subplots(nrows=row_count, ncols=col_count)
    axs = axs.reshape(-1)
    slice_loc = (volume.shape[0] // len(axs)) // 2
    for ax in axs.reshape(-1):
        ax.set_ylabel("Slice " + str("[" + str(slice_loc) + ":,:]"))
        ax.imshow(volume[slice_loc, :, :], cmap=colormap)
        slice_loc += (volume.shape[0] // len(axs))
    plt.show()


if __name__ == '__main__':
    exit(0)

