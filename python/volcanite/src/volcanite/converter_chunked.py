#  Copyright (C) 2024, Fabian Schiekel, Karlsruhe Institute of Technology
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

import converter as vc
import re

import numpy as np
import time
import threading

def __read_tmp_chunk_zy(chunk_information, volume_information):
    tmp_chunk = np.zeros(shape=volume_information['chunk_size_in'], dtype=volume_information['dtype_out'])

    tmp_chunk[:, :] = vc.read_volume(volume_information['path_in_format'].format(chunk_information['chunk_index'][2] - 1, chunk_information['chunk_index'][1] - 1, chunk_information['chunk_index'][0] - 1))

    return tmp_chunk


def __get_indices(chunk_information, volume_information, dim : int):
    current_element_read = 0

    chunk_information['start_element'][dim] = chunk_information['end_element'][dim]
    if current_element_read < volume_information['chunk_size_out'][dim]:
        # more elements must be read
        chunk_information['end_element'][dim] = min(volume_information['chunk_size_in'][dim] - chunk_information['end_element'][dim], volume_information['chunk_size_out'][dim] - current_element_read) + chunk_information['start_element'][dim]

        if chunk_information['end_element'][dim] == chunk_information['start_element'][dim]:
            current_element_read += volume_information['chunk_size_in'][dim]
            chunk_information['start_element'][dim] = 0
            chunk_information['chunk_index'][dim] += 1
        else :
            current_element_read += chunk_information['end_element'][dim] - chunk_information['start_element'][dim]

        if current_element_read < volume_information['chunk_size_out'][dim]:
            # need another chunk
            # reset and recalculate end
            # chunk_information['start_element'] remains untouched -> start element remains as start in the first chunk to stitch together later on
            chunk_information['end_element'][dim] = volume_information['chunk_size_out'][dim] - current_element_read
            # mark array, s.t. there are two chunks in dimension dim, which must be read
            chunk_information['stitch'][dim] = True

    # subtract current element read count from global to read count
    volume_information['elements_to_read'][dim] -= current_element_read


def __load_current_chunk(chunk_information, volume_information, dim):
    tmp_chunk = __read_tmp_chunk_zy(chunk_information, volume_information)

    return tmp_chunk


def __load_next_chunk(chunk_information, volume_information, dim):
    if chunk_information['stitch'][dim]:
        tmp_chunk_index = chunk_information['chunk_index'].copy()
        chunk_information['chunk_index'][dim] += 1
        tmp_chunk = __read_tmp_chunk_zy(chunk_information, volume_information)
        chunk_information['chunk_index'] = tmp_chunk_index
    else:
        raise Exception("only call load_next_chunk() in need of the next chunk -> stitch[dim] has to be true ")

    return tmp_chunk


def __load_chunks(chunk_information, volume_information, data):
    # TODO reload only if needed
    data[0] = __load_current_chunk(chunk_information, volume_information, 0)

    if chunk_information['chunks_needed'][1]:
        data[1] = __load_next_chunk(chunk_information, volume_information, 1)
    if chunk_information['chunks_needed'][2]:
        data[2] = __load_next_chunk(chunk_information, volume_information, 0)
    if chunk_information['chunks_needed'][3]:
        # increase chunk_index temporarily
        tmp_chunk_index = chunk_information['chunk_index'].copy()
        chunk_information['chunk_index'][0] += 1
        chunk_information['chunk_index'][1] += 1
        data[3] = __load_current_chunk(chunk_information, volume_information, 0)
        chunk_information['chunk_index'] = tmp_chunk_index


def __get_slice_zy(chunk_information, volume_information, data, current_slice_idx):
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


def __stitch_zy_chunks_together(chunk_information, volume_information, start : int, end : int, slice_count_offset : int, stitched_chunk, data, thread_id):
    tmp_stitched_chunk = np.empty(shape=(volume_information["chunk_size_out"][0], volume_information["chunk_size_out"][1], end-start))
    for slice_count in range(0, end-start):
        current_slice_idx = slice_count + start
        tmp = __get_slice_zy(chunk_information, volume_information, data, current_slice_idx)

        tmp_stitched_chunk[:, :, slice_count] = tmp
    stitched_chunk[:, :, start + slice_count_offset:end + slice_count_offset] = tmp_stitched_chunk


def __launch_threads(chunk_information, volume_information, slice_count_offset, global_start, global_end : int,
                     slices_per_thread : int, thread_count : int, stitched_chunk, data):
    threads = []
    start = global_start
    end = slices_per_thread + start
    remainder = global_end - thread_count * slices_per_thread

    for thread_idx in range(0, thread_count):
        thread = threading.Thread(target=__stitch_zy_chunks_together, args=(chunk_information, volume_information, start,
                                                                            end, slice_count_offset, stitched_chunk, data, thread_idx))

        start += slices_per_thread + (1 if thread_idx > 0 and thread_idx-1 < remainder else 0)
        end += slices_per_thread + (1 if thread_idx < remainder else 0)


        threads.append(thread)
        thread.start()
        print(f"thread {thread_idx} started")

    for thread in threads:
        thread.join()


def __calculate_volume_dim(chunk_size_in: tuple[int, int, int], last_chunk: str):
    volume_size = re.findall(r"\d+", last_chunk.split(".")[0])
    return np.array(chunk_size_in, dtype=int) * np.array(volume_size, dtype=int) + vc.read_volume(last_chunk).shape


def __update_and_reset_params(volume_information, chunk_information, dim, chunk_size_out, volume_dim):
    if chunk_information['stitch'][dim]:
        chunk_information['chunk_index'][dim] += 1
    chunk_information['stitch'][dim] = False
    volume_information['chunk_size_out'][dim] = chunk_size_out[dim].copy()
    if chunk_size_out[dim] > volume_information['elements_to_read'][dim] > 0:
        volume_information['chunk_size_out'][dim] = volume_information['elements_to_read'][dim]

    if dim == 1:
        chunk_information['chunks_needed'][1] = False
        chunk_information['chunks_needed'][3] = False
        volume_information['elements_to_read'][2] = volume_dim[2].copy()
    if dim == 0:
        chunk_information['chunks_needed'][1] = False
        chunk_information['chunks_needed'][2] = False
        chunk_information['chunks_needed'][3] = False
        volume_information['elements_to_read'][1] = volume_dim[1].copy()


def is_volume_conversion_valid(chunk_size_in, chunk_size_out, volume_dim):
    if any(chunk_size_in * 2 < chunk_size_out):
        return False
    if any(volume_dim < chunk_size_out):
        return False
    return True

def convert_chunked_volume(path_in_format: str, chunk_size_in: tuple[int, int, int], last_chunk: str,
                           path_out_format: str, chunk_size_out: tuple[int, int, int], thread_count: int = 16,
                           dtype_out=None):
    volume_dim = __calculate_volume_dim(chunk_size_in, last_chunk)

    # TODO: regarding axis orders: numpy volumes returned by read_volume are in ZYX shape (by np convention). This means
    #  that read_volume().shape = (DIM_Z, DIM_Y, DIM_X). write_volume() expects the same ZYX shape of the np array, but
    #  chunk file indices x{}y{}z{} are in XYZ order

    # construct each output chunk from up to 8 input chunks

    # first and last dimension is swapped: numpy arrays are in ZYX, but files are written / chunk sizes are given in XYZ
    chunk_size_out = np.array((chunk_size_out[2], chunk_size_out[1], chunk_size_out[0]))
    chunk_size_in = np.array((chunk_size_in[2], chunk_size_in[1], chunk_size_in[0]))
    volume_dim = np.array((volume_dim[2], volume_dim[1], volume_dim[0]))

    if not is_volume_conversion_valid(chunk_size_in, chunk_size_out, volume_dim):
        raise ValueError("Conversion is not valid, chunk_size_in should be less than twice as large as chunk_size_out and volume_dim should be larger than chunk_size_out")
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
                          'chunk_size_out': chunk_size_out.copy(),
                          'elements_to_read': volume_dim.copy(),
                          'dtype_out' : dtype_out
                          }
    chunk_information = {'chunk_index' : np.array((1, 1, 1)), # which chunk
                         'start_element' : np.array((0, 0, 0)), # start of interval
                         'end_element' : np.array((0, 0, 0)), # end of chunk or end in what I need for next chunk
                         'stitch' : np.array((0, 0, 0)), # need another chunk in dim
                         'chunks_needed' : np.full(shape=4, fill_value=False, dtype=bool),
                         }
    data = [np.array([], dtype=dtype_out) for _ in range(4)]
    for z in range(0, volume_dim[0], chunk_size_out[0]):
        chunk_information['chunk_index'][1] = 1
        chunk_information['end_element'][1] = 0
        __get_indices(chunk_information, volume_information, 0)

        if chunk_information['stitch'][0]:
            chunk_information['chunks_needed'][2] = True
        for y in range(0, volume_dim[1], chunk_size_out[1]):
            chunk_information['chunk_index'][2] = 1
            chunk_information['end_element'][2] = 0
            __get_indices(chunk_information, volume_information, 1)

            if chunk_information['stitch'][1]:
                chunk_information['chunks_needed'][1] = True
            for x in range(0, volume_dim[2], chunk_size_out[2]):
                __get_indices(chunk_information, volume_information, 2)
                if chunk_information['stitch'][0] and chunk_information['stitch'][1]:
                    chunk_information['chunks_needed'][3] = True

                __load_chunks(chunk_information, volume_information, data)
                stitched_chunk = np.empty(shape=volume_information['chunk_size_out'], dtype=dtype_out)

                elements_in_first_chunk = min(chunk_size_in[2] - chunk_information['start_element'][2], chunk_size_out[2])
                slices_per_thread = elements_in_first_chunk // thread_count
                __launch_threads(chunk_information, volume_information, -chunk_information['start_element'][2], chunk_information['start_element'][2], elements_in_first_chunk, slices_per_thread, thread_count, stitched_chunk, data)

                if elements_in_first_chunk < volume_information['chunk_size_out'][2]:
                    # need another chunk in x dim, also increase chunk_index in x dim
                    chunk_information['chunk_index'][2] += 1
                    __load_chunks(chunk_information, volume_information, data)

                    elements_in_second_chunk = chunk_size_out[2] - elements_in_first_chunk
                    slices_per_thread = elements_in_second_chunk // thread_count
                    __launch_threads(chunk_information, volume_information, elements_in_first_chunk, 0, elements_in_second_chunk, slices_per_thread, thread_count, stitched_chunk, data)

                print(f"write volume x{x// chunk_size_out[2]}y{y// chunk_size_out[1]}z{z// chunk_size_out[0]}")
                stitched_chunk = np.swapaxes(stitched_chunk, 0, 2)
                vc.write_volume(stitched_chunk, path_out_format.format(x // chunk_size_out[2], y // chunk_size_out[1], z // chunk_size_out[0]))

                volume_information['chunk_size_out'][2] = chunk_size_out[2].copy()
                if volume_information['elements_to_read'][2] < chunk_size_out[2] and volume_information['elements_to_read'][2] > 0:
                    volume_information['chunk_size_out'][2] = volume_information['elements_to_read'][2]
            __update_and_reset_params(volume_information, chunk_information, 1, chunk_size_out, volume_dim)
        __update_and_reset_params(volume_information, chunk_information, 0, chunk_size_out, volume_dim)


def write_chunked_volume(volume: np.ndarray, path_out_format: str, chunk_size: tuple[int, int, int]) -> None:
    """Exports the volume to a set of files where each file is a volume chunk with dimensions chunk_size^3. The file
    output format is selected based on the file extension of path_out_format. path_out_format must contain exactly
     three python string format keys that will be replaced with x y z chunk indices. e.g. 'my_volume_x{}y{}z{}.raw'."""

    if vc.__get_format_key_count(path_out_format) != 1:
        raise Exception("File path must contain exactly 1 python string format key")

    for z in range(0, volume.shape[0], chunk_size[0]):
        for y in range(0, volume.shape[1], chunk_size[1]):
            for x in range(0, volume.shape[2], chunk_size[2]):
                print("Writing " + path_out_format.format(x // chunk_size[2], y // chunk_size[1], z // chunk_size[0]))
                vc.write_volume(volume[z:(min(volume.shape[0], z + chunk_size[0])),
                             y:(min(volume.shape[1], y + chunk_size[1])),
                             x:(min(volume.shape[2], x + chunk_size[2]))],
                             path_out_format.format(x // chunk_size[2], y // chunk_size[1], z // chunk_size[0]))


def read_chunked_volume(path_out_format: str, chunk_count) -> np.ndarray:
    raise NotImplementedError("reading chunked volumes is not yet implemented")


if __name__ == '__main__':
    # example code
    # split chunks s.t three input chunks results in four output chunks, split in the first dimension
    shape = (1024, 1024, 1024)
    new_shape = (768, 1024, 1024)
    print("convert volume now")
    start_time = time.time()
    convert_chunked_volume("input/x{}y{}z{}.hdf5", shape, "input/x3y0z2.hdf5", "output/out_x{}y{}z{}.hdf5", new_shape, dtype_out=np.float32)
    end_time = time.time()

    elapsed_time_minutes = int((end_time - start_time) // 60)
    elapsed_time_seconds = (end_time - start_time) % 60
    print(f"diff: {elapsed_time_minutes} min {elapsed_time_seconds: .2f} sec")

    exit(0)