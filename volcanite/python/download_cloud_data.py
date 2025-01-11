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
            self.__dataset = array(self.__dataset_url, axis_order="XYZ")
            self.__axis_transpose_to_xyz = (2,1,0) # TODO: obtain axis order (XYZ, ZYX..) from data set
        elif self.__dataset_url[:2] == "gs":
            # TODO use intern[cloudvolume] to download from google cloud as well
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
            self.__axis_transpose_to_xyz = (2,1,0) # TODO: obtain axis order (XYZ, ZYX..) from data set
        else:
            raise ValueError("unknown cloud storage")

    def __init__(self, data_url: str):
        """Obtains a handle to the given bossdb or tensorstore cloud data set."""

        self.__axis_transpose_to_xyz = (0,1,2)
        self.__dataset = None
        self.__dataset_url = data_url
        self.__obtain_cloud_dataset()

    def get_dataset(self):
        return self.__dataset

    def get_shape(self):
        return self.__dataset.shape

    def read_chunk(self, start : tuple[int, int, int] | np.ndarray, end : tuple[int, int, int] | np.ndarray):
        result = np.array(self.__dataset[start[0]:end[0],
                                       start[1]:end[1],
                                       start[2]:end[2]], dtype='uint32')
        return result.transpose(self.__axis_transpose_to_xyz).reshape(end - start, order='C')

    def check_test_vis(self, origin : tuple[int, int, int] | None = None, chunk_size : tuple[int, int, int] | None = (1024, 1024, 1024)):
        full_dim = np.array(self.__dataset.shape)
        chunk_size = np.clip(np.array(chunk_size, dtype='int'), (1, 1, 1), full_dim // 2)
        origin = np.clip(full_dim // 2 - chunk_size // 2, (0,0,0), full_dim) if origin is None else np.clip(origin, (0,0,0), full_dim)
        print(origin, chunk_size)
        converter.debug_vis(self.read_chunk(origin, origin + chunk_size))

    def download(self, output_dir : Path, volume_size: tuple[int, int, int] | None = None, output_name: str = "x{}y{}z{}.{}",
                 origin : tuple[int, int, int] | None = None, chunk_size : tuple[int, int, int] | None = (1024, 1024, 1024),
                 output_format : str = "hdf5", continue_download: bool = False):

        if not output_format in converter.supported_formats():
            raise ValueError(f"unknown output format {output_format} is not in " + ",".join(converter.supported_formats()))

        # determine and clip (default) arguments, start / end volume sizes
        # and make our life easier: just convert everything to numpy arrays
        full_dim = np.array(self.__dataset.shape)
        volume_size = full_dim if volume_size is None else np.clip(volume_size, (0,0,0), full_dim)
        origin = np.clip(full_dim // 2 - volume_size // 2, (0,0,0), full_dim) if origin is None else np.clip(origin, (0,0,0), full_dim)
        total_end = np.clip(full_dim, origin, origin + volume_size)
        chunk_size = np.clip((1024, 1024, 1024), (0,0,0), volume_size) if chunk_size is None else np.clip(chunk_size, (0,0,0), volume_size)

        chunk_count = np.ceil(np.array([total_end[0] - origin[0], total_end[1] - origin[1], total_end[2] - origin[2]]) / np.array(chunk_size)).astype("uint32")
        total_chunk_count = int(chunk_count[0] * chunk_count[1] * chunk_count[2])

        # create output directory
        output_dir.mkdir(parents=True, exist_ok=True)
        info_file_path = output_dir / Path(output_name.format(int(chunk_count[0]) - 1,
                                                              int(chunk_count[1]) - 1,
                                                              int(chunk_count[2]) - 1, "txt"))
        if not info_file_path.exists():
            continue_download = False
        if not continue_download and os.listdir(output_dir):
            if not continue_download:
                raise IOError(f"output directory {output_dir} must be empty when starting new download")

        if volume_size[0] <= 0 or volume_size[1] <= 0 or volume_size[2] <= 0:
            raise ValueError("volume_size dimensions must be positive")
        if origin[0] < 0 or origin[1] < 0 or origin[2] < 0:
            raise ValueError("origin dimensions must be non-negative")
        if chunk_size[0] <= 0 or chunk_size[1] <= 0 or chunk_size[2] <= 0:
            raise ValueError("chunk_size dimensions must be positive")
        if (chunk_size[0] % 64) or (chunk_size[1] % 64) or (chunk_size[2] % 64):
            print("WARNING: chunk size should be dividable by 64 in each dimension for Volcanite compatibility.")

        print(f"Downloading from {full_dim} volume {self.__dataset_url} to "
              f"{(output_dir / Path(output_name.format('[X]', '[Y]', '[Z]', output_format)))}\n"
              f"sub-volume: {origin}:{total_end}, chunk size {chunk_size}")

        total_gb = (total_end[0] - origin[0]) * (total_end[1] - origin[1]) * (total_end[2] - origin[2]) * 4 / 1024 / 1024 / 1024
        if total_gb > 2048:
            confirm = input(f"WARNING: attempting to download volume with (uncompressed) size of {total_gb} GiB. Continue? (y/n) ").lower()
            if confirm != 'y':
                exit(1)
        chunk_gb = chunk_size[0] * chunk_size[1] * chunk_size[2] * 4 / 1024 / 1024 / 1024
        if chunk_gb > 4:
            confirm = input(f"WARNING: attempting to download volume as chunks with an (uncompressed) size of up to {chunk_gb} GiB per file. Continue? (y/n) ").lower()
            if confirm != 'y':
                exit(1)

        print(f"{time.strftime("%H:%M:%S")} {"Continue" if continue_download else "Start"} download of"
              f" {total_chunk_count} chunks. Uncompressed uint32 array is {total_gb} GiB.")
        time.sleep(2)

        # write an information file
        with open(info_file_path, 'w') as readme:
            readme.write(f"{time.strftime("%Y.%m.%d %H:%M:%S")} downloaded from {data_set_url}\n")
            readme.write(f"original volume has size {full_dim}.\n\ndownloaded volume")
            readme.write(f"  subset size: {volume_size}\n")
            readme.write(f"  subset region: {origin} to {total_end}\n")
            readme.write(f"  chunk size: {chunk_size}\n")
            readme.write(f"  chunk count: {chunk_count}\n")
            readme.write(f"  format: {output_format}\n")
            readme.close()

        chunk_id = 0
        for idx_2 in range(0, total_end[2] - origin[2], chunk_size[2]):
            for idx_1 in range(0, total_end[1] - origin[1], chunk_size[1]):
                for idx_0 in range(0, total_end[0] - origin[0], chunk_size[0]):
                    offset = np.array((idx_0, idx_1, idx_2))
                    start = origin + offset
                    end = np.clip(start + chunk_size, start, total_end)

                    print(f"{time.strftime("%H:%M:%S")} {(int(chunk_id / total_chunk_count * 100.)):02} % processing chunk "
                          f"x{idx_0 // chunk_size[0]}y{idx_1 // chunk_size[1]}z{idx_2 // chunk_size[2]} from "
                          f"{start} to {end} ({end - start})", end='')

                    output_file = output_dir / Path(output_name.format(idx_0 // chunk_size[0],
                                                                       idx_1 // chunk_size[1],
                                                                       idx_2 // chunk_size[2],
                                                                       output_format))

                    if not output_file.exists():
                        cur_slice = self.read_chunk(start, end)
                        converter.write_volume(cur_slice, str(output_file.resolve()), "uint32", True, False)
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
    parser.add_argument("-d", "--directory", help="empty/non-existing directory where data is stored.")
    parser.add_argument("-s", "--size", type=int, nargs=3, help="size of downloaded volume in voxels (default: full volume).")
    parser.add_argument("-t", "--filetype", default="hdf5", help="file type in which chunks are stored.")
    parser.add_argument("-o", "--origin", type=int, nargs=3, help="origin of the sub-volume in the full data set.")
    parser.add_argument("-c", "--chunk_size", type=int, nargs=3, default=(1024,1024,1024), help="volume is split into chunks of this size. should be dividable by 64.")
    parser.add_argument("-a", "--append", action="store_true", default=False, help="ignore non-empty output directory and skip existing chunk files.")
    parser.add_argument("-n", "--name", help="file name prefix for chunks that will be extended to [name]_x{}y{}z{}.[filetype]")
    parser.add_argument("-v", "--verbose", action="store_true")

    args = parser.parse_args()

    example_data = {'h01': "gs://h01-release/data/20210601/c3/",
                    "witvliet2020": "bossdb://witvliet2020/Dataset_8/segmentation",
                    }
    if args.data_set == "list-examples":
        print("Available short keys for data_set argument:\n  " + "\n  ".join(example_data.keys()))
        exit(0)
    data_set_url = example_data[args.data_set] if args.data_set in example_data else args.data_set
    if not args.name is None:
        if not args.name:
            output_name = ""
        else:
            output_name = args.name + "_"
    elif args.data_set in example_data:
        output_name = args.data_set + "_"
    else:
        output_name = ""
    output_name = output_name + "x{}y{}z{}.{}"

    # obtain dataset
    data = CloudDataDownload(data_set_url)

    # if no output directory is given, only print the shape of the volume if it is accessible
    if args.directory is None:
        print(f"Volume {data_set_url} is available with size {data.get_shape()}, order {data.get_order()}. "
              f"Specify a download directory with -d /path/to/dir/")
        # show a quick visualization to check if the indexing order is correct
        data.check_test_vis(origin=None, chunk_size=np.clip(args.chunk_size, (1,1,1), (1024,1024,1024)))
        exit(0)
    else:
        data.download(output_dir=Path(args.directory), output_name=output_name, output_format=args.filetype,
                      volume_size=args.size, origin=args.origin,
                      chunk_size=args.chunk_size, continue_download=args.append)
        exit(0)
