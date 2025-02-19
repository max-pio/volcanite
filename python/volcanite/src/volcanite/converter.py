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

import os
import string
import typing

import numpy as np
from vtkmodules.vtkCommonDataModel import vtkImageData
from vtkmodules.vtkIOXML import vtkXMLImageDataReader, vtkXMLImageDataWriter
from vtkmodules.util.numpy_support import vtk_to_numpy, numpy_to_vtk
import h5py
import PIL.Image as Image
import nibabel as nib
import gzip

import matplotlib
import matplotlib.pyplot as plt

from pathlib import Path

# Some notes regarding axis ordering:
# - Numpy assumes a ZYX indexing - at all times! a volume has a shape of volume.shape = [DIM_Z, DIM_Y, DIM_Z]
#
# A 2D example of an array with shape (2,3):
#     x -->
# y      0   1   2
# |  0 [[1., 0., 0.],
# V  1  [0., 1., 2.]]
#
# - All the read_* functions must return a numpy.ndarray that follows this dimensional ordering
# - All write_* functions receive a numpy.ndarray and must assume the same ordering,
#   but the writers may write out data in another axis order depending on their output file type.
# - Any python function operating on chunked data (internally) must as well assume this ordering for the chunk indexing:
#

########################################################################################################################
#                                            READER / WRITER PER FORMAT                                                #
########################################################################################################################

# vraw (Volcanite simplified raw format)
def read_vraw(path_in: str | os.PathLike) -> np.ndarray:
    with open(path_in, "rb") as file:
        # read header
        shape_str = file.readline()[:-1].decode('utf8').split()
        filetype = file.readline()[:-1].decode('utf8')
        # read binary payload
        vraw_volume = np.fromfile(file, dtype=filetype)
        vraw_volume = vraw_volume.reshape([int(shape_str[2]), int(shape_str[1]), int(shape_str[0])])
    return vraw_volume


def write_vraw(volume: np.ndarray, path_out: str | os.PathLike, dtype = None) -> None:
    volume = __guard_volume_dtype(volume, dtype)
    with open(path_out, "wb") as file:
        # write two line header:
        # [DimX] [DimY] [DimZ]
        # [data type]
        file.write(f"{volume.shape[2]} {volume.shape[1]} {volume.shape[0]}\n".encode('utf8'))
        file.write(f"{volume.dtype}\n".encode('utf8'))
        # write binary (tofile() always writes in C order)
        # for z in range(volume.shape[0]):
        #     for y in range(volume.shape[1]):
        #         for x in range(volume.shape[2]):
        #             file.write(volume[z, y, x])
        volume.astype(volume.dtype).tofile(file)


# NRRD4
def read_nrrd(path_in: str | os.PathLike) -> np.ndarray:
    raise NotImplementedError("reading NRRD files not yet implemented")


def write_nrrd(volume: np.ndarray, path_out: str | os.PathLike, dtype = None) -> None:
    volume = __guard_volume_dtype(volume, dtype)
    with open(path_out, "wb") as file:
        # write header:
        file.write("NRRD0004\n".encode('utf8'))
        file.write(("type: " + str(volume.dtype) + "\n").encode('utf8'))
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
        np.ascontiguousarray(volume.astype(volume.dtype)).tofile(file)


# HDF5
def read_hdf5(path_in: str | os.PathLike, key_path: list[str] | None = None) -> np.ndarray:
    f = h5py.File(path_in, 'r')
    # obtain the volume in xyz shape
    if key_path is None:
        # TODO: iterate through groups until first ndarray is found
        key_path = [list(f.keys())[0]]
    # iterate through keys
    _data = f[key_path[0]]
    for i in range(1, len(key_path)):
        _data = _data[key_path[i]]
    volume = _data[()]
    # return it in zyx shape (numpy convention)
    return volume.reshape((volume.shape[2], volume.shape[1], volume.shape[0]))

def write_hdf5(volume: np.ndarray, path_out: str | os.PathLike, dtype = None) -> None:
    volume = __guard_volume_dtype(volume, dtype)
    shape = volume.shape
    with h5py.File(path_out, "w") as f:
        f.create_dataset("data", data=volume, shape=(shape[2], shape[1], shape[0]), compression="gzip")


# Sliced TIFF
def read_sliced_tiff(path_in_format: str, slice_axis: int = 0) -> np.ndarray:
    """read the volume from tiff slices with the path string. path_format must use python3 format to insert integer
    slice ids, e.g. 'my_volume_{}.tiff' will write files my_volume_0.tiff, my_volume_1.tiff ..."""

    if __get_format_key_count(path_in_format) != 1:
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


def write_sliced_tiff(volume: np.ndarray, path_out_format) -> None:
    raise NotImplementedError("writing sliced TIFF files not yet implemented")


# Sliced PNG
def read_sliced_png(path_in_format) -> np.ndarray:
    raise NotImplementedError("reading sliced PNG files not yet implemented")

def write_sliced_png(volume: np.ndarray, path_out_format : str) -> None:
    """Write the volume as 2D RGBA8 PNG files slices along the z-axis. Each of the RGBA channels stores 8 bits of the
     32 bit volume labels. The least significant 8 bits are stored in the red channel. path_format must use python3
     format to insert integer slice ids, e.g. 'my_volume_{}.png' will write files my_volume_0.png, my_volume_1.png ...
     """

    if __get_format_key_count(path_out_format) != 1:
        raise Exception("File path must contain exactly 1 python string format key")

    for z in range(volume.shape[0]):
        png_slice = np.stack([volume[z] % 256, (volume[z] / 256) % 256, (volume[z] / (256 * 256)) % 256,
                              (volume[z] / (256 * 256 * 256) % 256)], axis=-1)
        image = Image.fromarray(png_slice.astype('uint8'))
        image.save(path_out_format.format(z), 'png')

# numpy
def read_numpy(path_in: str | os.PathLike) -> np.ndarray:
    """Reads a volume from npy or npz numpy files. For npz files, returns the first numpy array of the archive."""
    if str(path_in).endswith(".npz"):
        volume_archive = np.load(path_in)
        return volume_archive[next(iter(volume_archive))]
    else:
        return np.load(path_in)

def write_numpy(volume: np.ndarray, path_out: str | os.PathLike, dtype = None, compressed: bool = True) -> None:
    volume = __guard_volume_dtype(volume, dtype)
    if compressed:
        np.savez_compressed(path_out, volume)
    else:
        np.save(path_out, volume)

# nifti
def read_nifti(path_in: str | os.PathLike) -> np.ndarray:
    img = nib.load(Path(path_in))
    return np.asarray(img.dataobj)

def write_nifti(volume: np.ndarray, path_out: str | os.PathLike, dtype = None) -> None:
    volume = __guard_volume_dtype(volume, dtype)
    nii_image = nib.Nifti2Image(volume, affine=np.eye(4))
    nib.save(nii_image, path_out)

# VTI
def read_vti(path_in: str | os.PathLike) -> np.ndarray:
    reader = vtkXMLImageDataReader()
    reader.SetFileName(path_in)
    reader.Update(None)
    image = reader.GetOutput()
    if image.GetCellData().GetNumberOfArrays() > 0:
        return vtk_to_numpy(image.GetCellData().GetArray(0)).reshape(np.array(image.GetDimensions()) - 1)
    elif image.GetPointData().GetNumberOfArrays() > 0:
        return vtk_to_numpy(image.GetPointData().GetArray(0)).reshape(image.GetDimensions())
    else:
        raise IOError("Could not find any cell or point data in vtk image.")

def write_vti(volume: np.ndarray, path_out: str | os.PathLike, dtype = None, as_cell_data: bool = False) -> None:
    volume = __guard_volume_dtype(volume, dtype)

    image = vtkImageData()
    flat_data_array = volume.flatten()
    vtk_data = numpy_to_vtk(num_array=flat_data_array)

    if as_cell_data:
        image.GetCellData().SetScalars(vtk_data)
        image.SetDimensions(volume.shape[0] + 1, volume.shape[1] + 1, volume.shape[2] + 1)
    else:
        image.GetPointData().SetScalars(vtk_data)
        image.SetDimensions(volume.shape[0], volume.shape[1], volume.shape[2])

    writer = vtkXMLImageDataWriter()
    writer.SetFileName(path_out)
    writer.SetInputData(image)
    writer.Write()

########################################################################################################################
#                                               UTILITY FUNCTIONS                                                      #
########################################################################################################################

def __get_format_key_count(formatted_string: str) -> int:
    """:return: the number of python string format keys in formatted_string."""
    return len([f for f in string.Formatter().parse(formatted_string) if f[2] is not None])

def check_if_valid_axis_permutation(axis_order: str) -> str:
    axis_order = axis_order.lower()
    """raises a ValueError if axis_order is not a permutation of 'xyz'
    :returns: the axis order in lower case"""
    if len(axis_order) != 3 or not ('x' in axis_order and 'y' in axis_order and 'z' in axis_order):
        raise ValueError(f"axis order must be a permutation of 'xyz' but is {axis_order}")
    return axis_order

def reshape_memory_order(_volume: np.ndarray, from_order: str = 'zyx', to_order: str = 'zyx', verbose: bool = False) -> np.ndarray:
    from_order = check_if_valid_axis_permutation(from_order)
    to_order = check_if_valid_axis_permutation(to_order)

    if from_order == to_order:
        return _volume

    reshape_tuple = (from_order.find(to_order[0]), from_order.find(to_order[1]), from_order.find(to_order[2]))
    if verbose:
        print(f"Reshaping volume from {from_order} to {to_order}, reshape tuple {reshape_tuple}")
    return _volume.reshape((_volume.shape[reshape_tuple[0]],
                            _volume.shape[reshape_tuple[1]],
                            _volume.shape[reshape_tuple[2]]))

def copy_to_gzip(path_in: str | os.PathLike) -> Path:
    """For an input file volume.abc, creates a second file volume.abc.gz compressed with gzip DEFLATE.
    :returns: the path to the written compressed file"""

    path_out = Path(path_in).parent / (Path(path_in).name + '.gz')
    with open(path_in, 'rb') as file_in, gzip.open(path_out, 'wb') as file_out:
        file_out.writelines(file_in)

    return path_out

def copy_from_gzip(path_in: typing.Union[str, bytes, os.PathLike]) -> Path:
    """For an input file volume.abc.gz, creates a second file volume.abc decompressed from gzip DEFLATE.
    :returns: the path to the written uncompressed file"""

    if Path(path_in).suffix != ".gz":
        raise ValueError("Input file path for gzip decompression must end with .gz")

    path_out = Path(path_in).with_suffix("")
    with gzip.open(path_in, 'rb') as file_in, open(path_out, 'wb') as file_out:
        file_out.writelines(file_in)

    return path_out


def write_volume(volume: np.ndarray, path_out: str | os.PathLike, dtype = None,
                 input_axis_order: str = 'zyx', apply_gzip: bool = False) -> None:
    """Automatically selects the writer for the respective format based on the path_out file type.
    Volumes are always written in XYZ memory axis order for Volcanite compatibility."""

    extensions = [e.lower() for e in Path(path_out).suffixes]
    if len(extensions) == 0:
        raise ValueError("Output file path for writing volume must have a file type.")

    volume = reshape_memory_order(volume, input_axis_order, 'zyx')

    if extensions == [".vraw"] or extensions == [".raw"]:
        write_vraw(volume, path_out, dtype)
    elif extensions == [".nrrd"]:
        write_nrrd(volume, path_out, dtype)
    elif extensions == [".hdf5"] or extensions == [".h5"]:
        write_hdf5(volume, path_out, dtype)
    elif extensions == [".tiff"]:
        write_sliced_tiff(volume, path_out)
    elif extensions == [".png"]:
        write_sliced_png(volume, path_out)
    elif extensions == [".npy"]:
        write_numpy(volume, path_out, dtype, False)
    elif extensions == [".npz"]:
        write_numpy(volume, path_out, dtype, True)
    elif extensions == [".nii"] or extensions == [".nii", ".gz"]:
        write_nifti(volume, path_out, dtype)
    elif extensions == [".vti"]:
        write_vti(volume, path_out, dtype)
    else:
        raise Exception("unknown segmentation volume file extension " + "".join(extensions))

    if apply_gzip:
        # zip the file, delte the uncompressed initial file
        copy_to_gzip(path_out)
        Path(path_out).unlink()


def read_volume(path_in: str | os.PathLike, input_axis_order: str = 'zyx') -> np.array:
    """Automatically selects the reader for the respective format based on the path_in file type.
    Returns a numpy array reshaped to axis order ZYX."""

    extensions = [e.lower() for e in Path(path_in).suffixes]
    if len(extensions) == 0:
        raise ValueError("Input file path for reading volume must have a file type.")

    apply_gzip: bool = (extensions[-1] == ".gz" and len(extensions) > 1 and extensions[-2] != ".nii")
    if apply_gzip:
        # create a temporary decompressed file from which the volume will be loaded
        path_in = copy_from_gzip(path_in)

    if extensions[-1] == ".vraw" or extensions[-1] == ".raw":
        _volume_in = read_vraw(path_in)
    elif extensions[-1] == ".nrrd":
        _volume_in = read_nrrd(path_in)
    elif extensions[-1] == ".hdf5" or extensions[-1] == ".h5":
        _volume_in = read_hdf5(path_in)
    elif extensions[-1] == ".tif" or extensions[-1] == ".tiff":
        _volume_in = read_sliced_tiff(path_in)
    elif extensions[-1] == ".png":
        _volume_in = read_sliced_png(path_in)
    elif extensions[-1] == ".npy" or extensions[-1] == ".npz":
        _volume_in = read_numpy(path_in)
    elif extensions[-1] == ".nii" or extensions[-2:] == [".nii", ".gz"]:
        _volume_in = read_nifti(path_in)
    elif extensions[-1] == ".vti":
        _volume_in = read_vti(path_in)
    else:
        raise Exception("unknown segmentation volume file extension " + "".join(extensions))

    if apply_gzip:
        # remove temporary uncompressed file
        path_in.unlink()

    _volume_in = reshape_memory_order(_volume_in, input_axis_order, 'zyx')

    return _volume_in


def __guard_volume_dtype(volume: np.ndarray, dtype) -> np.ndarray:
    """If dtype is not None, converts the volume to the given dtype with safeguards:
       1) if dtype is an unsigned type but volume contains values < 0, the values are offset to be 0 at minimum,
       2) if volume contains values outside the range of dtype, the values are normalized to that interval."""

    if not dtype or volume.dtype.num == np.dtype(dtype).num:
        return volume

    supported_min = np.uint64(np.iinfo(dtype).min)
    supported_max = np.uint64(np.iinfo(dtype).max)
    vol_min = np.min(volume).astype('uint64')
    vol_max = np.max(volume).astype('uint64')

    if (supported_max - supported_min) < (vol_max - vol_min):
        print("G1 Converting volume with range [" + str(vol_min) + "," + str(vol_max) + "] to type " + str(dtype)
              + " by normalization to range [" + str(supported_min) + "," + str(supported_max) + "].")
        volume = (volume - vol_min) / (vol_max - vol_min) * (supported_max - supported_min) + supported_min
    elif vol_min < supported_min:
        print("G2 Converting volume with range [" + str(vol_min) + "," + str(vol_max) + "] to type " + str(dtype)
              + " by offsetting values to [" + str(supported_min) + "," + str(vol_max - vol_min + supported_min) + "].")
        volume = volume - vol_min + supported_min
    elif vol_max > supported_max:
        print("G3 Converting volume with range [" + str(vol_min) + "," + str(vol_max) + "] to type " + str(dtype)
              + " by offsetting values to [" + str(vol_min - vol_max + supported_max) + "," + str(supported_max) + "].")
        volume = volume - vol_max + supported_max

    return volume.astype(dtype)


def convert_volume(path_in: str | os.PathLike, path_out: str | os.PathLike, input_axis_order: str = 'zyx', dtype=None) -> None:
    """Automatically selects the writer for the respective format based on the path_out file type.
    Volumes are always written in XYZ memory axis order for Volcanite compatibility."""

    write_volume(read_volume(path_in, input_axis_order=input_axis_order), path_out=path_out, dtype=dtype, input_axis_order='zyx')

def debug_print(volume: np.ndarray) -> None:
    print("volume with shape " + str(volume.shape) + " type " + str(volume.dtype)
          + " min. " + str(np.min(volume)) + " max. " + str(np.max(volume)))

def debug_vis(volume: np.ndarray, row_count: int = 2, col_count: int = 3, colormap: str = 'turbo',
              print_info: bool = True) -> None:
    """Plot (row_count * col_count) 2D slices of the segmentation volume."""

    if print_info:
        debug_print(volume)
    # create a set of subplots displaying slices of the volume
    fig, axs = plt.subplots(nrows=row_count, ncols=col_count)
    axs = axs.reshape(-1)
    slice_loc = (volume.shape[0] // len(axs)) // 2
    for ax in axs.reshape(-1):
        ax.set_ylabel("Slice " + str("[" + str(slice_loc) + ":,:]"))
        ax.imshow(volume[slice_loc, :, :], cmap=colormap, interpolation='nearest')
        slice_loc += (volume.shape[0] // len(axs))


    if matplotlib.is_interactive():
        plt.show()
    else:
        print("matplotlib backend is non-interactive. Trying to save plot as ./converter_plot.png\n"
              "To enable interactive plotting, set the environment variable MPLBACKEND to an available backend, e.g.\n"
              "> pip install PyQt6\n> export MPLBACKEND=qtagg\n> python3 converter.py")
        plt.savefig('./converter_plot.png')

def supported_formats() -> list[str]:
    """Returns a list of supported segmentation volume file formats."""
    return ["vraw", "raw", "nrrd", "hdf5", "h5", "tiff", "png", "npy", "npz", "nii", "nii.gz", "vti"]
