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
from pathlib import Path

import converter

class CloudDataDownload:
    def __obtain_cloud_dataset(self):
        """Obtains the array handle to a remote data set without downloading the full data until a slice is accessed."""

        # see online examples:
        # https://colab.research.google.com/gist/jbms/1ec1192c34ec816c2c517a3b51a8ed6c/h01_data_access.ipynb#scrollTo=rtimT0EkY93k
        # https://bossdb.org/get-started
        if self.__dataset_url[:6] == "bossdb":
            # obtain data set from bossdb
            self.__dataset = array(self.__dataset_url)
        elif self.__dataset_url[:2] == "gs":
            # obtain data set form google storage
            context = ts.Context({'cache_pool': {'total_bytes_limit': 1000000000}})
            _path = self.__dataset_url[5:]
            _gs_bucket = _path[:_path.find("/")]
            _gs_path = _path[_path.find("/") + 1:]
            self.__dataset = ts.open({'driver': 'neuroglancer_precomputed',
                                      'kvstore': {'driver': 'gcs', 'bucket': _gs_bucket},
                                      'path': _gs_path,
                                      'scale_metadata': {'resolution': [8, 8, 33]}},
                                      read=True, context=context).result()[ts.d['channel'][0]]
        else:
            raise ValueError("unknown cloud storage")
        # print("Accessing data set " + str(path) + " with shape (ZYX) " + str(_dataset.shape) + " chunks: " + str(np.ceil(np.array(_dataset.shape) / CHUNK_SIZE).astype('int')))

    def __init__(self, data_url: str):
        """Obtains a handle to the given bossdb or tensorstore cloud data set."""

        self.__dataset_url = data_url
        self.__obtain_cloud_dataset()

    def get_shape(self) -> tuple[int, int, int]:
        """:returns: the dimensions of the full volume."""
        return self.__dataset.shape

    def download(self, output_dir : Path, volume_size: tuple[int, int, int] | None = None,
                 origin : tuple[int, int, int] | None = None, chunk_size : tuple[int, int, int] | None = (1024, 1024, 1024),
                 output_format : str = "hdf5"):

        if not output_format in converter.supported_formats():
            raise ValueError(f"unknown output format {output_format} is not in " + ",".join(converter.supported_formats()))

        # determine start / end volume sizes
        full_dim = self.get_shape()
        if volume_size is None:
            volume_size = full_dim
        if chunk_size is None:
            chunk_size = full_dim
        if origin is None:
            origin = (max(0, self.__dataset.shape[0] // 2 - volume_size[0] // 2),
                      max(0, self.__dataset.shape[1] // 2 - volume_size[1] // 2),
                      max(0, self.__dataset.shape[2] // 2 - volume_size[2] // 2))
        end = (min(full_dim[0], origin[0] + volume_size[0]),
               min(full_dim[1], origin[1] + volume_size[1]),
               min(full_dim[2], origin[2] + volume_size[2]))

        # create output directory
        output_dir.mkdir(parents=True, exist_ok=True)
        if os.listdir(output_dir):
            raise IOError(f"output directory {output_dir} must be empty")
        if volume_size[0] <= 0 or volume_size[1] <= 0 or volume_size[2] <= 0:
            raise ValueError("volume_size dimensions must be positive")
        if origin[0] < 0 or origin[1] < 0 or origin[2] < 0:
            raise ValueError("origin dimensions must be non-negative")
        if chunk_size[0] <= 0 or chunk_size[1] <= 0 or chunk_size[2] <= 0:
            raise ValueError("chunk_size dimensions must be positive")
        if (chunk_size[0] % 64) or (chunk_size[1] % 64) or (chunk_size[2] % 64):
            print("WARNING: chunk size should be dividable by 64 in each dimension for Volcanite compatibility.")

        print(f"Downloading from {volume_size} volume {self.__dataset_url} to {output_dir}\n"
              f"sub-volume: {origin}:{end}, chunk size {chunk_size}")

        chunk_count = np.ceil(np.array([end[0] - origin[0], end[1] - origin[1], end[2] - origin[2]]) / np.array(chunk_size))
        total_chunk_count = chunk_count[0] * chunk_count[1] * chunk_count[2]

        total_gb = (end[0] - origin[0]) * (end[1] - origin[1]) * (end[2] - origin[2]) * 4 / 1024 / 1024 / 1024
        if total_gb > 2048:
            confirm = input(f"WARNING: attempting to download volume with (uncompressed) size of {total_gb} GB. Continue? (y/n)").lower()
            if confirm != 'y':
                exit(1)
        chunk_gb = chunk_size[0] * chunk_size[1] * chunk_size[2] * 4 / 1024 / 1024
        if chunk_gb > 4:
            confirm = input(f"WARNING: attempting to download volume as chunks with an (uncompressed) size of {chunk_gb} GB per file. Continue? (y/n)").lower()
            if confirm != 'y':
                exit(1)

        print(f"{time.strftime("%H:%M:%S")} Start download of {int(total_chunk_count)} chunks. Uncompressed uint32"
              f" array is {total_gb} GB.")
        chunk_id = 0
        for z in range(0, end[0] - origin[0], chunk_size[0]):
            for y in range(0, end[1] - origin[1], chunk_size[1]):
                for x in range(0, end[2] - origin[2], chunk_size[2]):
                    z_end = min(full_dim[0], z + chunk_size[0] + origin[0])
                    y_end = min(full_dim[1], y + chunk_size[1] + origin[1])
                    x_end = min(full_dim[2], x + chunk_size[2] + origin[2])

                    print(f"{time.strftime("%H:%M:%S")} {int(chunk_id / total_chunk_count * 100.)} % processing chunk "
                          f"x{x // chunk_size[2]}y{y // chunk_size[1]}z{z // chunk_size[0]} from ZYX "
                          f"{(z + origin[0], y + origin[1], z + origin[2])}"
                          f" to {(z_end + origin[0], y_end + origin[1], x_end + origin[2])}", end='')

                    output_file = output_dir / Path("x{}y{}z{}.{}".format(x // chunk_size[2], y // chunk_size[1], z // chunk_size[0], output_format))
                    if not output_file.exists():
                        cur_slice = np.array(self.__dataset[(z + origin[0]):z_end, (y + origin[1]):y_end, (x + origin[2]):x_end]).astype('uint32')
                        converter.write_volume(cur_slice, output_file, "uint32", True, False)
                        print(" done.")
                    else:
                        print(" already exists. skipping.")

                    chunk_id += 1

        print("=============================")
        print(time.strftime("%H:%M:%S") + "  done")


if __name__ == '__main__':
    parser = argparse.ArgumentParser(
        prog='Segmentation Volume Downloader',
        description='Downloads segmentation volumes from cloud storages and stores them locally.',
        epilog='')

    parser.add_argument("data_set", help="data set url or example name. 'list-examples' lists available names.")
    parser.add_argument("-o", "--output_dir", help="empty/non-existing directory where data is stored.")
    parser.add_argument("-s", "--size", type=int, nargs=3, help="size of downloaded volume in voxels (default: full volume).")
    parser.add_argument("-f", "--filetype", default="hdf5", help="file type in which chunks are stored.")
    parser.add_argument("-o", "--origin", help="origin of the sub-volume in the full data set")
    parser.add_argument("-c", "--chunk_size", type=int, nargs=3, default=(1024,1024,1024), help="volume is split into chunks of this size. should be dividable by 64.")
    parser.add_argument("-v", "--verbose", action="store_true")

    args = parser.parse_args()

    example_data = {'h01': "gs://h01-release/data/20210601/c3/",
                    "witvliet2020": "bossdb://witvliet2020/Dataset_8/segmentation"}
    if args.data_set == "list-examples":
        print("Available short keys for data_set argument:\n  " + "\n  ".join(example_data.keys()))
        exit(0)
    data_set_url = example_data[args.data_set] if args.data_set in example_data else args.data_set

    # obtain dataset
    data = CloudDataDownload(data_set_url)

    # if no output directory is given, only print the shape of the volume if it is accessible
    if args.output_dir is None:
        print("Volume " + data_set_url + " is available with a size of " + str(data.get_shape())
              + ". Specify a download directory with -o /path/to/dir/")
        exit(0)
    else:
        data.download(args.output_dir, args.size, args.origin, args.chunk_size)
        exit(0)

