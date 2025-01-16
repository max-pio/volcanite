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
import time

# Ensure tensorstore does not attempt to use GCE credentials
os.environ['GCE_METADATA_ROOT'] = 'metadata.google.internal.invalid'
import tensorstore as ts

from intern import array
import numpy as np
import converter

# ----------- CONFIG --------------
# data set to download (browse bossdb.org for available data)
# DATASET = "bossdb://witvliet2020/Dataset_8/segmentation"
DATASET = "gs://h01-release/data/20210601/c3/"

# total (cubic) volume size to download
TOTAL_SIZE = (2048, 2048, 2048)

# chunk size in all three dimensions
CHUNK_SIZE = (1024, 1024, 1024)

# location to store the resulting files
output_path = "data/download/"

# format of the resulting files
output_format = "hdf5"

# ---------------------------------

def obtain_cloud_dataset(path):
    """Returns an array handle to a remote data set without downloading the full data until a slice is accessed."""

    # see online examples:
    # https://colab.research.google.com/gist/jbms/1ec1192c34ec816c2c517a3b51a8ed6c/h01_data_access.ipynb#scrollTo=rtimT0EkY93k
    # https://bossdb.org/get-started
    if path[:6] == "bossdb":
        # obtain data set from bossdb
        _dataset = array(path)
    elif path[:2] == "gs":
        # obtain data set form google storage
        context = ts.Context({'cache_pool': {'total_bytes_limit': 1000000000}})
        _path = path[5:]
        _gs_bucket = _path[:_path.find("/")]
        _gs_path = _path[_path.find("/")+1:]
        _dataset =  ts.open({'driver': 'neuroglancer_precomputed',
                                  'kvstore': {'driver': 'gcs', 'bucket': _gs_bucket},
                                  'path': _gs_path,
                                  'scale_metadata': {'resolution': [8, 8, 33]}},
                                  read=True, context=context).result()[ts.d['channel'][0]]
    else:
        assert False, "unknown cloud storage"
    # print("Accessing data set " + str(path) + " with shape (ZYX) " + str(_dataset.shape) + " chunks: " + str(np.ceil(np.array(_dataset.shape) / CHUNK_SIZE).astype('int')))
    return _dataset


if __name__ == '__main__':

    parser = argparse.ArgumentParser(
        prog='Segmentation Volume Downloader',
        description='Downloads segmentation volumes from cloud storages and stores them locally.',
        epilog='')

    parser.add_argument('-d', '--data_set', default='h01')
    parser.add_argument('-o', '--output_path', default='')
    parser.add_argument('-s', '--size', type=int, nargs=3, default=(2048, 2048, 2048))
    parser.add_argument('-f', '--filetype', default='hdf5')
    parser.add_argument('-c', '--chunk_size', type=int, nargs=3, default=(1024, 1024, 1024))
    parser.add_argument('-v', '--verbose', action='store_true')

    args = parser.parse_args()
    if args.data_set == 'h01':
        DATASET = "gs://h01-release/data/20210601/c3/"
    elif args.data_set == 'witvliet':
        DATASET = "bossdb://witvliet2020/Dataset_8/segmentation"
    else:
        print("Unknown data set " + args.data_set)
        DATASET = args.data_set

    output_path = args.output_path
    output_format = args.filetype
    TOTAL_SIZE = args.size
    CHUNK_SIZE = args.chunk_size

    if (CHUNK_SIZE[0] % 64) or (CHUNK_SIZE[1] % 64) or (CHUNK_SIZE[2] % 64):
        print("WARNING: chunk size should be dividable by 64 in each dimension for brick-wise compression")
        exit(2)

    # obtain dataset
    data = obtain_cloud_dataset(DATASET)
    full_dim_z = data.shape[0]
    full_dim_y = data.shape[1]
    full_dim_x = data.shape[2]

    if output_path == '':
        print("Volume " + DATASET + " has a total size of " + str((full_dim_z, full_dim_y, full_dim_x))
              + ". Specify a download directory with -o /path/to/dir/")
        exit(0)

    # create output directory
    if len(output_path) == 0:
        output_path = "./" + DATASET[DATASET.find("://") + 3:] + "/"
    os.makedirs(output_path, exist_ok=True)
    if output_path[-1] != '/' and output_path[-1] != '\\':
        output_path += '/'
    if os.listdir(output_path):
        print("Aborting: directory " + output_path + " must be empty")
        exit(0)

    # determine start / end volume sizes
    start = (max(0, data.shape[0] // 2 - TOTAL_SIZE[0] // 2),
             max(0, data.shape[1] // 2 - TOTAL_SIZE[1] // 2),
             max(0, data.shape[2] // 2 - TOTAL_SIZE[2] // 2))
    end   = (min(full_dim_z, start[0] + TOTAL_SIZE[0]),
             min(full_dim_y, start[1] + TOTAL_SIZE[1]),
             min(full_dim_x, start[2] + TOTAL_SIZE[2]))

    print("Downloading " + str(
          TOTAL_SIZE) + " volume from " + DATASET + " to " + output_path
          + "\nsub-volume: " + str(start) + ":" + str(end) + ", chunk size " + str(CHUNK_SIZE))

    chunk_count = np.ceil(np.array([end[0] - start[0], end[1] - start[1], end[2] - start[2]]) / np.array(CHUNK_SIZE))
    total_chunk_count = chunk_count[0] * chunk_count[1] * chunk_count[2]

    print(time.strftime("%H:%M:%S") + "  Start download of " + str(int(total_chunk_count)) + " chunks. Uncompressed uint32 array is " + str((end[0] - start[0]) * (end[1] - start[1]) * (end[2] - start[2]) * 4 / 1024 / 1024 / 1024) + " GB.")
    chunk_id = 0
    for z in range(0, end[0] - start[0], CHUNK_SIZE[0]):
        for y in range(0, end[1] - start[1], CHUNK_SIZE[1]):
            for x in range(0, end[2] - start[2], CHUNK_SIZE[2]):
                z_end = min(full_dim_z, z + CHUNK_SIZE[0] + start[0])
                y_end = min(full_dim_y, y + CHUNK_SIZE[1] + start[1])
                x_end = min(full_dim_x, x + CHUNK_SIZE[2] + start[2])

                print(time.strftime("%H:%M:%S") + "  " + str(int(chunk_id / total_chunk_count * 100.)) + "% processing chunk x"
                      + str(x // CHUNK_SIZE[2]) + "y" + str(y // CHUNK_SIZE[1]) + "z" + str(z // CHUNK_SIZE[0])
                      + " from ZYX " + str((z + start[0], y + start[1], z + start[2])) + " to " + str((z + end[0], y + end[1], z + end[2])) , end='')

                output_file = output_path + "x" + str(x // CHUNK_SIZE[2]) + "y" + str(y // CHUNK_SIZE[1]) + "z" + str(z // CHUNK_SIZE[0]) + "." + output_format
                if not os.path.exists(output_file):
                    cur_slice = np.array(data[(z + start[0]):z_end, (y + start[1]):y_end, (x + start[2]):x_end]).astype('uint32')

                    converter.write_volume(cur_slice, output_file, "uint32", True, False)
                    print(" done.")
                else:
                    print(" already exists. skipping.")

                chunk_id  += 1

    print("=============================")
    print(time.strftime("%H:%M:%S") + "  done")
