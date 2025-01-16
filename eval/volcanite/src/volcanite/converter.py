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

import argparse
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
        file.write(
            (str(volume.shape[2]) + " " + str(volume.shape[1]) + " " + str(volume.shape[0]) + "\n").encode('utf8'))
        file.write((str(volume.dtype) + "\n").encode('utf8'))
        # write binary (tofile() always writes in C order)
        np.ascontiguousarray(volume.astype(volume.dtype)).tofile(file)
        # for z in range(volume.shape[0]):
        #     for y in range(volume.shape[1]):
        #         for x in range(volume.shape[2]):
        #             file.write(volume[z, y, x])


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
def read_hdf5(path_in: str | os.PathLike) -> np.ndarray:
    f = h5py.File(path_in, 'r')
    key = list(f.keys())[0]
    return f[key][()]


def write_hdf5(volume: np.ndarray, path_out: str | os.PathLike, dtype = None) -> None:
    volume = __guard_volume_dtype(volume, dtype)
    with h5py.File(path_out, "w") as f:
        f.create_dataset("data", shape=volume.shape, dtype=volume.dtype, data=volume.data, compression="gzip")


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

    #    for z in tqdm(range(labels.shape[0])):
    for z in range(volume.shape[0]):
        slice = np.stack([volume[z] % 256, (volume[z] / 256) % 256, (volume[z] / (256 * 256)) % 256,
                          (volume[z] / (256 * 256 * 256) % 256)], axis=-1)
        image = Image.fromarray(slice.astype('uint8'))
        image.save(path_out_format.format(z), 'png', compress_level=9)

# numpy
def read_numpy(path_in: str | os.PathLike) -> np.ndarray:
    """Reads a volume from npy or npz numpy files. For npz files, returns the first numpy array of the archive."""
    if path_in.endswith(".npz"):
        volume_archive = np.load(path_in)
        return next(iter(volume_archive))
    else:
        return np.load(path_in)

def write_numpy(volume: np.ndarray, path_out: str | os.PathLike, dtype = None, compressed: bool = True) -> None:
    volume = __guard_volume_dtype(volume, dtype)
    if compressed:
        np.savez(path_out, volume)
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

def __guarantee_c_order(_volume: np.ndarray) -> np.ndarray:
    if np.isfortran(_volume):
        return np.reshape(_volume.flatten(order='F'), shape=_volume.shape, order='C')
    else:
        return _volume

def copy_to_gzip(path_in: typing.Union[str, bytes, os.PathLike]) -> Path:
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


def write_volume(volume: np.ndarray, path_out: str | os.PathLike, dtype=None,
                 guaranteee_c_order:bool=True, apply_gzip:bool=False) -> None:
    """Automatically selects the writer for the respective format based on the path_out file type."""

    extensions = [e.lower() for e in Path(path_out).suffixes]
    if len(extensions) == 0:
        raise ValueError("Output file path for writing volume must have a file type.")

    if guaranteee_c_order:
        volume = __guarantee_c_order(volume)

    if extensions == [".vraw"] or extensions == [".raw"]:
        write_vraw(volume, path_out, dtype)
    elif extensions == [".nrrd"]:
        write_nrrd(volume, path_out, dtype)
    elif extensions == [".hdf5"] or extensions == [".h5"]:
        write_hdf5(volume, path_out, dtype)
    elif extensions == [".tiff"]:
        write_sliced_tiff(volume, path_out, dtype)
    elif extensions == [".png"]:
        write_sliced_png(volume, path_out, dtype)
    elif extensions == [".np"]:
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


def read_volume(path_in: str | os.PathLike) -> np.array:
    """Automatically selects the reader for the respective format based on the path_in file type."""

    extensions = [e.lower() for e in Path(path_in).suffixes]
    if len(extensions) == 0:
        raise ValueError("Input file path for reading volume must have a file type.")

    apply_gzip: bool = (extensions[-1] == ".gz" and len(extensions) > 1 and extensions[-2] != ".nii")
    if apply_gzip:
        # create a temporary decompressed file from which the volume will be loaded
        path_in = copy_from_gzip(path_in)

    if extensions == [".vraw"] or extensions == [".raw"]:
        _volume_in = read_vraw(path_in)
    elif extensions == [".nrrd"]:
        _volume_in = read_nrrd(path_in)
    elif extensions == [".hdf5"] or extensions == [".h5"]:
        _volume_in = read_hdf5(path_in)
    elif extensions == [".tiff"]:
        _volume_in = read_sliced_tiff(path_in)
    elif extensions == [".png"]:
        _volume_in = read_sliced_png(path_in)
    elif extensions == [".np"] or extensions == [".npz"]:
        _volume_in = read_numpy(path_in)
    elif extensions == [".nii"] or extensions == [".nii", ".gz"]:
        _volume_in = read_nifti(path_in)
    elif extensions == [".vti"]:
        _volume_in = read_vti(path_in)
    else:
        raise Exception("unknown segmentation volume file extension " + "".join(extensions))

    if apply_gzip:
        # remove temporary uncompressed file
        path_in.unlink()

    return _volume_in


#  chunked export
def write_chunked_volume(volume: np.ndarray, path_out_format : str, chunk_size : tuple[int, int, int]) -> None:
    """Exports the volume to a set of files where each file is a volume chunk with dimensions chunk_size^3. The file
    output format is selected based on the file extension of path_out_format. path_out_format must contain exactly
     three python string format keys that will be replaced with x y z chunk indices. e.g. 'my_volume_x{}y{}z{}.raw'."""
    if __get_format_key_count(path_out_format) != 1:
        raise Exception("File path must contain exactly 1 python string format key")

    for z in range(0, volume.shape[0], chunk_size[0]):
        for y in range(0, volume.shape[1], chunk_size[1]):
            for x in range(0, volume.shape[2], chunk_size[2]):
                print("Writing " + path_out_format.format(x // chunk_size[2], y // chunk_size[1], z // chunk_size[0]))
                write_volume(volume[z:(min(volume.shape[0], z + chunk_size[0])),
                             y:(min(volume.shape[1], y + chunk_size[1])),
                             x:(min(volume.shape[2], x + chunk_size[2]))],
                             path_out_format.format(x // chunk_size[2], y // chunk_size[1], z // chunk_size[0]))

def read_chunked_volume(path_out_format: str) -> np.ndarray:
    raise NotImplementedError("reading chunked volumes is not yet implemented")

def convert_chunked_volume(path_in_format: str, chunk_size_in: tuple[int, int, int], volume_dim: tuple[int, int, int],
                           path_out_format: str, chunk_size_out: tuple[int, int, int], dtype_out=None) -> None:
    raise NotImplementedError("reading chunked volumes is not yet implemented")

    # construct each output chunk from up to 8 input chunks
    tmp_chunk = np.empty(shape=volume_dim, dtype=dtype_out)
    for z in range(0, volume_dim[0], chunk_size_out[0]):
        for y in range(0, volume_dim[1], chunk_size_out[1]):
            for x in range(0, volume_dim[2], chunk_size_out[2]):
                # TODO: read all input chunks that are relevant for the output chunk, stitch together, and offset / clip
                # tmp_chunk = ...

                # export output chunk
                # TODO: clamp chunk dimension of border chunks with respect to total volume size
                write_volume(tmp_chunk,
                             path_out_format.format(x // chunk_size_out[2], y // chunk_size_out[1], z // chunk_size_out[0]))


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
        print("1 Converting volume with range [" + str(vol_min) + "," + str(vol_max) + "] to type " + str(dtype)
              + " by normalization to range [" + str(supported_min) + "," + str(supported_max) + "].")
        volume = (volume - vol_min) / (vol_max - vol_min) * (supported_max - supported_min) + supported_min
    elif vol_min < supported_min:
        print("2 Converting volume with range [" + str(vol_min) + "," + str(vol_max) + "] to type " + str(dtype)
              + " by offsetting values to [" + str(supported_min) + "," + str(vol_max - vol_min + supported_min) + "].")
        volume = volume - vol_min + supported_min
    elif vol_max > supported_max:
        print("3 Converting volume with range [" + str(vol_min) + "," + str(vol_max) + "] to type " + str(dtype)
              + " by offsetting values to [" + str(vol_min - vol_max + supported_max) + "," + str(supported_max) + "].")
        volume = volume - vol_max + supported_max

    return volume.astype(dtype)


def convert_volume(path_in: str | os.PathLike, path_out: str | os.PathLike, dtype=None) -> None:
    write_volume(read_volume(path_in), path_out, dtype)

def debug_print(volume: np.ndarray) -> None:
    print("volume with shape " + str(volume.shape) + " type " + str(volume.dtype)
          + " min. " + str(np.min(volume)) + " max. " + str(np.max(volume)))

def debug_vis(volume: np.ndarray, row_count: int = 2, col_count: int = 3, colormap:str = 'turbo',
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
        ax.imshow(volume[slice_loc, :, :], cmap=colormap)
        slice_loc += (volume.shape[0] // len(axs))


    if matplotlib.is_interactive():
        plt.show()
    else:
        print("matplotlib backend is non-interactive. Trying to save plot as ./converter_plot.png\n"
              "To enable interactive plotting, set the environment variable MPLBACKEND to an available backend, e.g.\n"
              "> pip install PyQt6\n> export MPLBACKEND=qtagg\n> python3 converter.py")
        plt.savefig('./converter_plot.png')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(
        prog='Segmentation Volume Converter',
        description='Downloads segmentation volumes from cloud storages and stores them locally.',
        epilog='')

    parser.add_argument('input_file', help='path to input volume file')
    parser.add_argument('output_file', help='path to output volume file')
    parser.add_argument('-z', '--gzip', action='store_true', help="apply additional gzip compression on output file")
    parser.add_argument('--vis', action='store_true', help="show a 2D plot of volume slices after import")
    parser.add_argument('-v', '--verbose', action='store_true', help="enable verbose output")
    parser.add_argument('--guarantee-corder',  default=True, action=argparse.BooleanOptionalAction, help="enable guard functions to guarantee a C indexing output order")

    args = parser.parse_args()

    volume_in = read_volume(args.input_file)
    if args.verbose:
        debug_print(volume_in)
    if args.vis:
        debug_vis(volume_in)

    write_volume(volume_in, args.output_file, 'uint32', args.guarantee_corder, args.gzip)
    exit(0)

