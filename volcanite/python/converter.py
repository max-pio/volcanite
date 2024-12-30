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

import numpy as np
from vtkmodules.vtkCommonDataModel import vtkImageData
from vtkmodules.vtkIOXML import vtkXMLImageDataReader, vtkXMLImageDataWriter
from vtkmodules.util.numpy_support import vtk_to_numpy, numpy_to_vtk
import h5py
import PIL.Image as Image
import nibabel as nib
import gzip

import time
import threading
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


def write_vraw(volume, out_path, dtype=None):
    volume = guard_volume_dtype(volume, dtype)
    with open(out_path, "wb") as file:
        # write two line header:
        # [DimX] [DimY] [DimZ]
        # [data type]
        file.write(
            (str(volume.shape[2]) + " " + str(volume.shape[1]) + " " + str(volume.shape[0]) + "\n").encode('utf8'))
        file.write((str(volume.dtype) + "\n").encode('utf8'))
        # write binary
        np.ascontiguousarray(volume.astype(volume.dtype)).tofile(file)
        # for z in range(volume.shape[0]):
        #     for y in range(volume.shape[1]):
        #         for x in range(volume.shape[2]):
        #             file.write(volume[z, y, x])


# NRRD4
def read_nrrd(path_in):
    raise NotImplementedError("reading NRRD files not yet implemented")


def write_nrrd(volume, out_path, dtype=None):
    volume = guard_volume_dtype(volume, dtype)
    with open(out_path, "wb") as file:
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
def read_hdf5(path_in):
    f = h5py.File(path_in, 'r')
    key = list(f.keys())[0]
    return f[key][()]


def write_hdf5(volume, path_out, dtype=None):
    volume = guard_volume_dtype(volume, dtype)
    with h5py.File(path_out, "w") as f:
        f.create_dataset("data", shape=volume.shape, dtype=volume.dtype, data=volume.data, compression="gzip")


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

def write_numpy(volume, path_out, dtype=None, compressed = True):
    volume = guard_volume_dtype(volume, dtype)
    if compressed:
        volume.savez(path_out)
    else:
        volume.save(path_out)

# nifti
def read_nifti(path_in):
    return np.array(nib.load(path_in).dataobj)

def write_nifti(volume, path_out, dtype=None):
    volume = guard_volume_dtype(volume, dtype)
    nii_image = nib.Nifti2Image(volume, affine=np.eye(4))
    nib.save(nii_image, path_out)

# VTI
def read_vti(path_in):
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

def write_vti(volume, path_out, dtype=None, as_cell_data=False):
    volume = guard_volume_dtype(volume, dtype)

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

def strip_file_extension(path):
    return os.path.splitext(path)[0]

def guarantee_c_order(_volume):
    if np.isfortran(_volume):
        return np.reshape(_volume.flatten(order='F'), _volume.shape)
    else:
        return _volume

def copy_as_gzip(path_in):
    """For an input file volume.abc, creates a second file volume.abc.gz compressed with gzip DEFLATE."""

    with open(path_in, 'rb') as file_in, gzip.open(path_in + ".gz", 'wb') as file_out:
        file_out.writelines(file_in)


def write_volume(volume, path_out, dtype=None, guaranteee_c_order=True, apply_gzip=False):
    """Automatically selects the writer for the respective format based on path_out file extension."""

    if guaranteee_c_order:
        volume = guarantee_c_order(volume)

    extension = path_out[(path_out.rfind('.') + 1):].lower()

    if extension == "vraw" or extension == "raw":
        write_vraw(volume, path_out, dtype)
    elif extension == "nrrd":
        write_nrrd(volume, path_out, dtype)
    elif extension == "hdf5" or extension == "h5":
        write_hdf5(volume, path_out, dtype)
    elif extension == "tiff":
        write_sliced_tiff(volume, path_out, dtype)
    elif extension == "png":
        write_sliced_png(volume, path_out, dtype)
    elif extension == "np":
        write_numpy(volume, path_out, dtype, False)
    elif extension == "npz":
        write_numpy(volume, path_out, dtype, True)
    elif extension == "nii" or path_out.endswith("nii.gz"):
        write_nifti(volume, path_out, dtype)
    elif extension == "vti":
        write_vti(volume, path_out, dtype)
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


def read_tmp_chunk_zy(chunk_information, volume_information):
    tmp_chunk = np.zeros(shape=volume_information['chunk_size_in'], dtype=volume_information['dtype_out'])

    tmp_chunk[:, :] = read_volume(volume_information['path_in_format'].format(chunk_information['chunk_index'][2] - 1, chunk_information['chunk_index'][1] - 1, chunk_information['chunk_index'][0] - 1))

    return tmp_chunk


def get_indices(chunk_information, volume_information, dim : int):
    element_read = 0

    chunk_information['start_element'][dim] = chunk_information['end_element'][dim]
    if element_read < volume_information['chunk_size_out'][dim]:
        # more elements must be read
        chunk_information['end_element'][dim] = min(volume_information['chunk_size_in'][dim] - chunk_information['end_element'][dim], volume_information['chunk_size_out'][dim] - element_read) + chunk_information['start_element'][dim]

        if chunk_information['end_element'][dim] == chunk_information['start_element'][dim]:
            element_read += volume_information['chunk_size_in'][dim]
            chunk_information['start_element'][dim] = 0
            chunk_information['chunk_index'][dim] += 1
        else :
            element_read += chunk_information['end_element'][dim] - chunk_information['start_element'][dim]

        if element_read < volume_information['chunk_size_out'][dim]:
            # need another chunk
            # reset and recalculate end
            # chunk_information['start_element'] remains untouched -> start element remains as start in the first chunk to stitch together later on
            chunk_information['end_element'][dim] = volume_information['chunk_size_out'][dim] - element_read
            # mark array, s.t. there are two chunks in dimension dim, which must be read
            chunk_information['stitch'][dim] = True

def load_current_chunk(chunk_information, volume_information, dim):
    tmp_chunk = read_tmp_chunk_zy(chunk_information, volume_information)

    return tmp_chunk


def load_next_chunk(chunk_information, volume_information, dim):
    if chunk_information['stitch'][dim]:
        tmp_chunk_index = chunk_information['chunk_index'].copy()
        chunk_information['chunk_index'][dim] += 1
        tmp_chunk = read_tmp_chunk_zy(chunk_information, volume_information)
        chunk_information['chunk_index'] = tmp_chunk_index
    else:
        raise Exception("only call load_next_chunk() in need of the next chunk -> stitch[dim] has to be true ")

    return tmp_chunk


def load_chunks(chunk_information, volume_information, data):
    # TODO reload only if needed
    data[0] = load_current_chunk(chunk_information, volume_information, 0)

    if chunk_information['chunks_needed'][1]:
        data[1] = load_next_chunk(chunk_information, volume_information, 1)
    if chunk_information['chunks_needed'][2]:
        data[2] = load_next_chunk(chunk_information, volume_information, 0)
    if chunk_information['chunks_needed'][3]:
        # increase chunk_index temporarily
        tmp_chunk_index = chunk_information['chunk_index'].copy()
        chunk_information['chunk_index'][0] += 1
        chunk_information['chunk_index'][1] += 1
        data[3] = load_current_chunk(chunk_information, volume_information, 0)
        chunk_information['chunk_index'] = tmp_chunk_index


def get_slice_zy(chunk_information, volume_information, data, current_slice_idx):
    chunk_size_out, chunk_size_in = volume_information['chunk_size_out'], volume_information['chunk_size_in']
    stitch, start_element, end_element = chunk_information['stitch'], chunk_information['start_element'], chunk_information['end_element']

    tmp_chunk = np.empty(shape=(chunk_size_out[0], chunk_size_out[1]), dtype=volume_information['dtype_out'])
    elements_in_first_chunk = chunk_size_in - start_element
    if stitch[0] and stitch[1]:
        # upper left
        tmp_chunk[:elements_in_first_chunk[0], :elements_in_first_chunk[1]] = data[0][start_element[0]:,chunk_size_in[1] - end_element[1]:, current_slice_idx]
        # upper right
        tmp_chunk[:elements_in_first_chunk[0], elements_in_first_chunk[1]:] = data[1][chunk_size_in[0] - end_element[0]:, 0:chunk_size_out[1] - elements_in_first_chunk[1], current_slice_idx]
        # lower left
        tmp_chunk[elements_in_first_chunk[0]:, :elements_in_first_chunk[1]] = data[2][:elements_in_first_chunk[1], start_element[1]:, current_slice_idx]
        # lower right
        tmp_chunk[elements_in_first_chunk[0]:, elements_in_first_chunk[1]:] = data[3][:elements_in_first_chunk[1], 0:chunk_size_out[1] - elements_in_first_chunk[1], current_slice_idx]
    elif stitch[0]:
        tmp_chunk[:elements_in_first_chunk[0], :] = data[0][start_element[0]:, start_element[1]:start_element[1] + chunk_size_out[1], current_slice_idx]
        tmp_chunk[elements_in_first_chunk[0]:, :] = data[2][:chunk_size_out[0] - elements_in_first_chunk[0], start_element[1]:start_element[1] + chunk_size_out[1], current_slice_idx]
    elif stitch[1]:
        tmp_chunk[:, :elements_in_first_chunk[1]] = data[0][start_element[0]:start_element[0] + chunk_size_out[0], start_element[1]:, current_slice_idx]
        tmp_chunk[:, elements_in_first_chunk[1]:] = data[1][start_element[0]:start_element[0] + chunk_size_out[0], :chunk_size_out[1] - elements_in_first_chunk[1], current_slice_idx]
    else:
        tmp_chunk[:chunk_size_out[0], :chunk_size_out[1]] = data[0][start_element[0]:end_element[0],start_element[1]:end_element[1],current_slice_idx]

    return tmp_chunk


def stitch_zy_chunks_together(chunk_information, volume_information, start : int, end : int, slice_count_offset : int, stitched_chunk, data, thread_id):
    tmp_stitched_chunk = np.empty(shape=(volume_information["chunk_size_out"][0], volume_information["chunk_size_out"][1], end-start))
    for slice_count in range(0, end-start):
        current_slice_idx = slice_count + start
        tmp = get_slice_zy(chunk_information, volume_information, data, current_slice_idx)

        tmp_stitched_chunk[:, :, slice_count] = tmp
    stitched_chunk[:, :, start + slice_count_offset:end + slice_count_offset] = tmp_stitched_chunk


def launch_threads(chunk_information, volume_information, slice_count_offset, global_start, global_end : int,
                   slices_per_thread : int, thread_count : int, stitched_chunk, data):
    threads = []
    start = global_start
    end = slices_per_thread + start
    remainder = global_end - thread_count * slices_per_thread

    for thread_idx in range(0, thread_count):
        thread = threading.Thread(target=stitch_zy_chunks_together, args=(chunk_information, volume_information, start,
                                                                          end, slice_count_offset, stitched_chunk, data, thread_idx))

        start += slices_per_thread + (1 if thread_idx > 0 and thread_idx-1 < remainder else 0)
        end += slices_per_thread + (1 if thread_idx < remainder else 0)


        threads.append(thread)
        thread.start()
        print(f"thread {thread_idx} started")

    for thread in threads:
        thread.join()


def is_volume_conversion_valid(chunk_size_in, chunk_size_out, volume_dim):
    if volume_dim[0] % chunk_size_in[0] != 0 or volume_dim[1] % chunk_size_in[1] != 0 or volume_dim[2] % chunk_size_in[2] != 0:
        return False

    if volume_dim[0] % chunk_size_out[0] != 0 or volume_dim[1] % chunk_size_out[1] != 0 or volume_dim[2] % chunk_size_out[2] != 0:
        return False

    return True


def convert_chunked_volume(path_in_format : str, chunk_size_in : (int, int, int), volume_dim : (int, int, int),
                           path_out_format : str, chunk_size_out : (int, int, int), thread_count=16, dtype_out=None):
    # construct each output chunk from up to 8 input chunks

    # first and last dimension is swapped
    #TODO change code, s.t no swapping is needed
    chunk_size_out = np.array((chunk_size_out[2], chunk_size_out[1], chunk_size_out[0]))
    chunk_size_in = np.array((chunk_size_in[2], chunk_size_in[1], chunk_size_in[0]))
    volume_dim = np.array((volume_dim[2], volume_dim[1], volume_dim[0]))

    if not is_volume_conversion_valid(chunk_size_in, chunk_size_out, volume_dim):
        raise "Conversion is not valid, check if the input/output dimension is a multiple of the volume dimension"
    # z
    # ^
    # |
    # |
    # |-----> y

    # chunks_needed -> indicates, what specific chunks are needed
    # each array indices corresponds to one (neighbour) chunk, chunk at index 0 is always the current chunk
    #   0    1
    #   2    3

    volume_information = {'path_in_format' : path_in_format,
                          'chunk_size_in' : chunk_size_in,
                          'volume_dim' : volume_dim,
                          'path_out_format' : path_out_format,
                          'chunk_size_out': chunk_size_out,
                          'dtype_out' : dtype_out
                          }
    chunk_information = {'chunk_index' : np.array((1, 1, 1)), # which chunk
                         'start_element' : np.array((0, 0, 0)), # start of interval
                         'end_element' : np.array((0, 0, 0)), # end of chunk or end in what I need for next chunk
                         'stitch' : np.array((0, 0, 0)), # need another chunk in dim
                         'chunks_needed' : np.full(shape=4, fill_value=False, dtype=bool),
                         }
    data = [np.array([], dtype=dtype_out) for _ in range(4)]
    stitched_chunk = np.empty(shape=chunk_size_out, dtype=dtype_out)
    for z in range(0, volume_dim[0], chunk_size_out[0]):
        chunk_information['chunk_index'][1] = 1
        chunk_information['end_element'][1] = 0
        get_indices(chunk_information, volume_information, 0)

        if chunk_information['stitch'][0]:
            chunk_information['chunks_needed'][2] = True
        for y in range(0, volume_dim[1], chunk_size_out[1]):
            chunk_information['chunk_index'][2] = 1
            chunk_information['end_element'][2] = 0
            get_indices(chunk_information, volume_information, 1)
            for x in range(0, volume_dim[2], chunk_size_out[2]):
                get_indices(chunk_information, volume_information, 2)
                if chunk_information['stitch'][1]:
                    chunk_information['chunks_needed'][1] = True

                if chunk_information['stitch'][0] and chunk_information['stitch'][1]:
                    chunk_information['chunks_needed'][3] = True
                load_chunks(chunk_information, volume_information, data)


                elements_in_first_chunk = min(chunk_size_in[2] - chunk_information['start_element'][2], chunk_size_out[2])
                slices_per_thread = elements_in_first_chunk // thread_count
                launch_threads(chunk_information, volume_information, -chunk_information['start_element'][2], chunk_information['start_element'][2], elements_in_first_chunk, slices_per_thread, thread_count, stitched_chunk, data)

                if elements_in_first_chunk < chunk_size_out[2]:
                    # need another chunk in x dim, also increase chunk_index in x dim
                    chunk_information['chunk_index'][2] += 1
                    load_chunks(chunk_information, volume_information, data)

                    elements_in_second_chunk = chunk_size_out[2] - elements_in_first_chunk
                    slices_per_thread = elements_in_second_chunk // thread_count
                    launch_threads(chunk_information, volume_information, elements_in_first_chunk, 0, elements_in_second_chunk, slices_per_thread, thread_count, stitched_chunk, data)

                print(f"write volume x{x// chunk_size_out[2]}y{y// chunk_size_out[1]}z{z// chunk_size_out[0]}")
                stitched_chunk = np.swapaxes(stitched_chunk, 0, 2)
                write_volume(stitched_chunk, path_out_format.format(x // chunk_size_out[2], y // chunk_size_out[1], z // chunk_size_out[0]))
                stitched_chunk = np.empty(shape=chunk_size_out, dtype=dtype_out)

            # increase chunk index if needed and resets chunk parameter
            if chunk_information['stitch'][1]:
                chunk_information['chunk_index'][1] += 1
            chunk_information['stitch'][1] = False
            chunk_information['chunks_needed'][1] = False
            chunk_information['chunks_needed'][3] = False
        # increase chunk index if needed and resets chunk parameter
        if chunk_information['stitch'][0]:
            chunk_information['chunk_index'][0] += 1
        chunk_information['stitch'][0] = False
        chunk_information['chunks_needed'][1] = False
        chunk_information['chunks_needed'][2] = False
        chunk_information['chunks_needed'][3] = False

    return stitched_chunk


def guard_volume_dtype(volume, dtype):
    """If dtype is not None, converts the volume to the given dtype with safeguards:
       1) if dtype is an unsigned type but volume contains values < 0, the values are offset to be 0 at minimum,
       2) if volume contains values outside the range of dtype, the values are normalized to that interval."""

    if not dtype or volume.dtype.num == np.dtype(dtype).num:
        return volume

    supported_min = np.iinfo(dtype).min
    supported_max = np.iinfo(dtype).max
    vol_min = np.min(volume)
    vol_max = np.max(volume)

    if (supported_max - supported_min) < (vol_max - vol_min):
        print("Converting volume with range [" + str(vol_min) + "," + str(vol_max) + "] to type " + str(dtype)
              + " by normalization to range [" + str(supported_min) + "," + str(supported_max) + "].")
        volume = (volume - vol_min) / (vol_max - vol_min) * (supported_max - supported_min) + supported_min
    elif vol_min < supported_min:
        print("Converting volume with range [" + str(vol_min) + "," + str(vol_max) + "] to type " + str(dtype)
              + " by offsetting values to [" + str(supported_min) + "," + str(vol_max - vol_min + supported_min) + "].")
        volume = volume - vol_min + supported_min
    elif vol_max > supported_min:
        print("Converting volume with range [" + str(vol_min) + "," + str(vol_max) + "] to type " + str(dtype)
              + " by offsetting values to [" + str(vol_min - vol_max + supported_max) + "," + str(supported_max) + "].")
        volume = volume - vol_max + supported_max

    return volume.astype(dtype)


def convert_volume(path_in, path_out, dtype=None):
    write_volume(read_volume(path_in), path_out, dtype)

def debug_print(volume):
    print("volume with shape " + str(volume.shape) + " type " + str(volume.dtype)
          + " min. " + str(np.min(volume)) + " max. " + str(np.max(volume)))

def debug_vis(volume, row_count=2, col_count=3, colormap='turbo', print_info=True):
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
    plt.show()


if __name__ == '__main__':
    # example code
    # split chunks s.t three input chunks results in four output chunks, split in the first dimension
    shape = (1024, 1024, 1024)
    vol_dim = (3*1024, 1024, 1024)
    new_shape = (768, 1024, 1024)
    print("convert volume now")
    start = time.time()
    convert_chunked_volume("input/x{}y{}z{}.hdf5", shape, vol_dim, "output/out_x{}y{}z{}.hdf5", new_shape, dtype_out=np.float32)
    end = time.time()

    elapsed_time_minutes = int((end - start) // 60)
    elapsed_time_seconds = (end - start) % 60
    print(f"diff: {elapsed_time_minutes} min {elapsed_time_seconds: .2f} sec")

    exit(0)



