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
import time

# Ensure tensorstore does not attempt to use GCE credentials
os.environ['GCE_METADATA_ROOT'] = 'metadata.google.internal.invalid'
import tensorstore as ts

from intern import array
import numpy as np
from pathlib import Path

from volcanite import converter

class CloudDataDownload:
    def __obtain_cloud_dataset(self):
        """Obtains the array handle to a remote data set without downloading the full data until a slice is accessed."""

        # see online examples:
        # https://colab.research.google.com/gist/jbms/1ec1192c34ec816c2c517a3b51a8ed6c/h01_data_access.ipynb#scrollTo=rtimT0EkY93k
        # https://bossdb.org/get-started
        if self.__dataset_url[:6] == "bossdb":
            # obtain data set from bossdb
            self.__dataset = array(self.__dataset_url, axis_order=self.__data_set_cfg["axis_order"].upper())
        elif self.__dataset_url[:2] == "gs":
            # TODO use intern[cloudvolume] to download from google cloud as well
            # obtain data set form google storage
            context = ts.Context({'cache_pool': {'total_bytes_limit': 1000000000}})
            _path = self.__dataset_url[5:]
            _gs_bucket = _path[:_path.find("/")]
            _gs_path = _path[_path.find("/") + 1:]
            self.__dataset = ts.open({'driver': 'neuroglancer_precomputed', # TODO: drivers may differ per data set
                                      'kvstore': {'driver': 'gcs', 'bucket': _gs_bucket},
                                      'path': _gs_path,
                                      },
                                      read=True, context=context).result()[ts.d['channel'][0]]
        # elif self.__dataset_url[:2] == "s3":
        #     self.__dataset = CloudVolume(self.__dataset_url, mip=0, use_https=True)
        else:
            raise ValueError("unknown cloud storage")

        # TODO: implement webdav client, e.g. for https://l4dense2019.brain.mpg.de/
        # TODO: implement datadryad client, e.g. for https://datadryad.org/stash/dataset/doi:10.5061/dryad.dfn2z351g

    def __init__(self, url: str, data_set_cfg: dict[str, str] | None = None):
        """Obtains a handle to the given bossdb or tensorstore cloud data set."""

        self.__data_set_cfg = data_set_cfg
        if not "axis_order" in data_set_cfg:
            data_set_cfg["axis_order"] = "xyz"
        else:
            converter.check_if_valid_axis_permutation(data_set_cfg["axis_order"])
        self.__axis_in_to_xyz = np.asarray([self.__data_set_cfg["axis_order"].find('x'),
                                           self.__data_set_cfg["axis_order"].find('y'),
                                           self.__data_set_cfg["axis_order"].find('z')], dtype="uint32")
        self.__axis_xyz_to_in = np.asarray(['xyz'.find(self.__data_set_cfg["axis_order"][0]),
                                            'xyz'.find(self.__data_set_cfg["axis_order"][1]),
                                            'xyz'.find(self.__data_set_cfg["axis_order"][2])], dtype="uint32")
        self.__dataset = None
        self.__dataset_url = url
        self.__obtain_cloud_dataset()

    def get_dataset(self):
        return self.__dataset

    def get_shape(self):
        """:returns: shape of the data set in the data set axis order."""
        return self.__dataset.shape[:3]

    def get_download_axis_order(self):
        return self.__data_set_cfg["axis_order"]

    def read_chunk(self, start : tuple[int, int, int] | np.ndarray, end : tuple[int, int, int] | np.ndarray):
        """Downloads a single chunk from the cloud volume. Input arguments are in the axis order of the data set. but
        the returned volume will always have a ZYX shape.
        :param start: inclusive first voxel of the chunk to download. Given in cloud data set axis order coordinates.
        :param end: exclusive first voxel of the chunk to download. Given in cloud data set axis order coordinates.
        :returns: the chunk [start, pos) given in the data set axis order, converted into a ZYX order numpy array"""
        result = np.array(self.__dataset[start[0]:end[0],
                                       start[1]:end[1],
                                       start[2]:end[2]], dtype='uint32')
        # drop last dimension(s) if necessary
        while len(result.shape) >= 4:
            result = result[:, :, :, 0]

        transpose_tuple = (self.__data_set_cfg["axis_order"].find('z'),
                           self.__data_set_cfg["axis_order"].find('y'),
                           self.__data_set_cfg["axis_order"].find('x'))
        return result.transpose(transpose_tuple)
        # TODO: this is a transpose not a reshape, right? results on H01 seem correct
        # return converter.reshape_memory_order(result, self.__data_set_cfg["axis_order"], 'zyx', verbose=False)

    def download(self, output_dir : str | os.PathLike, volume_size: tuple[int, int, int] | None = None, output_name: str = "x{}y{}z{}.{}",
                 origin : tuple[int, int, int] | None = None, chunk_size : tuple[int, int, int] | None = (1024, 1024, 1024),
                 output_format : str = "hdf5", continue_download: bool = False) -> tuple[int, int, int]:

        if not output_format in converter.supported_formats():
            raise ValueError(f"unknown output format {output_format} is not in " + ",".join(converter.supported_formats()))

        if any(d > 1 for d in self.__dataset.shape[3:]):
            raise ValueError(f"Data set shape must have no more than 3 (real) dimensions but is {self.__dataset.shape}")

        # determine and clip (default) arguments, start / end volume sizes
        # and make our life easier: just convert everything to numpy arrays
        # NOTE: any shape prefixed with in_* is specified in the axis_order of the data_set_cfg.
        in_full_dim = np.array(self.get_shape())
        in_volume_size = np.clip(volume_size, (0,0,0), in_full_dim) if volume_size else in_full_dim
        in_total_start = np.clip(origin, (0,0,0), in_full_dim) if origin else np.clip(in_full_dim // 2 - in_volume_size // 2, (0,0,0), in_full_dim)
        in_total_end = np.clip(in_full_dim, in_total_start, in_total_start + in_volume_size)

        # NOTE: any shape prefixed with out_* is specified in XYZ axis order
        out_volume_size = np.array([in_total_end[0] - in_total_start[0],
                                    in_total_end[1] - in_total_start[1],
                                    in_total_end[2] - in_total_start[2]])[self.__axis_in_to_xyz]
        out_chunk_size =  np.clip(chunk_size, (0,0,0), out_volume_size) if chunk_size else np.clip((1024, 1024, 1024), (0, 0, 0), out_volume_size)
        in_chunk_size = out_chunk_size[self.__axis_xyz_to_in]
        out_chunk_count = np.ceil(out_volume_size / np.array(out_chunk_size)).astype("uint32")
        total_chunk_count = int(out_chunk_count[0] * out_chunk_count[1] * out_chunk_count[2])

        # create output directory
        _output_dir = Path(output_dir)
        _output_dir.mkdir(parents=True, exist_ok=True)
        info_file_path = _output_dir / Path(output_name.format(int(out_chunk_count[0]) - 1,
                                                              int(out_chunk_count[1]) - 1,
                                                              int(out_chunk_count[2]) - 1, "txt"))
        if not info_file_path.exists():
            continue_download = False
        if not continue_download and os.listdir(_output_dir):
            if not continue_download:
                raise IOError(f"output directory {_output_dir} must be empty when starting new download")

        if in_volume_size[0] <= 0 or in_volume_size[1] <= 0 or in_volume_size[2] <= 0:
            raise ValueError("volume_size dimensions must be positive")
        if in_total_start[0] < 0 or in_total_start[1] < 0 or in_total_start[2] < 0:
            raise ValueError("origin dimensions must be non-negative")
        if chunk_size[0] <= 0 or chunk_size[1] <= 0 or chunk_size[2] <= 0:
            raise ValueError("chunk_size dimensions must be positive")
        if (chunk_size[0] % 64) or (chunk_size[1] % 64) or (chunk_size[2] % 64):
            print("WARNING: chunk size should be dividable by 64 in each dimension for Volcanite compatibility.")

        print(f"Downloading from cloud volume {self.__dataset_url}:"
              f"\n  sub-volume: {in_total_start}:{in_total_end} of total size {in_full_dim} [{self.__data_set_cfg["axis_order"]}]"
              f"\nWriting to chunked files {(_output_dir / Path(output_name.format(f"[0..{out_chunk_count[0] - 1}]",
                                                                                   f"[0..{out_chunk_count[1] - 1}]",
                                                                                   f"[0..{out_chunk_count[2] - 1}]",
                                                                                   output_format)))}:"
              f"\n volume {(in_total_end - in_total_start)[self.__axis_in_to_xyz]} with chunk size {out_chunk_size} [xyz]")

        total_gb = out_volume_size[0] * out_volume_size[1] * out_volume_size[2] * 4 / 1024 / 1024 / 1024
        if total_gb > 2048:
            confirm = input(f"WARNING: attempting to download volume with (uncompressed) size of {total_gb} GiB. Continue? (y/n) ").lower()
            if confirm != 'y':
                exit(1)
        chunk_gb = total_chunk_count * 4 / 1024 / 1024 / 1024
        if chunk_gb > 4:
            confirm = input(f"WARNING: attempting to download volume as chunks with an (uncompressed) size of up to {chunk_gb} GiB per file. Continue? (y/n) ").lower()
            if confirm != 'y':
                exit(1)

        print(f"{time.strftime("%H:%M:%S")} {"Continue" if continue_download else "Start"} download of"
              f" {total_chunk_count} chunks. Uncompressed uint32 array is {total_gb} GiB.")
        time.sleep(2)

        # write an information file
        with open(info_file_path, 'w') as readme:
            readme.write(f"{time.strftime("%Y.%m.%d %H:%M:%S")} downloaded from {self.__dataset_url}\n")
            readme.write(f"original volume has size {in_full_dim} [{self.__data_set_cfg["axis_order"]}].\n")
            readme.write(f"  subset region: {in_total_start} to {in_total_end}\n")
            readme.write(f"\ndownloaded volume [xyz]\n")
            readme.write(f"  subset size: {out_volume_size}\n")
            readme.write(f"  chunk size: {out_chunk_size}\n")
            readme.write(f"  chunk count: {out_chunk_count}\n")
            readme.write(f"  format: {output_format}\n")
            readme.write(f"  axis transpose: {self.__axis_in_to_xyz} [{self.__data_set_cfg["axis_order"]} cloud -> xyz export]\n")
            readme.close()

        chunk_id = 0
        # these indices are in
        for in_idx_2 in range(0, in_total_end[2] - in_total_start[2], in_chunk_size[2]):
            for in_idx_1 in range(0, in_total_end[1] - in_total_start[1], in_chunk_size[1]):
                for in_idx_0 in range(0, in_total_end[0] - in_total_start[0], in_chunk_size[0]):
                    in_offset = np.array((in_idx_0, in_idx_1, in_idx_2))
                    in_start = in_total_start + in_offset
                    in_end = np.clip(in_start + in_chunk_size, in_start, in_total_end)

                    # compute the output chunk index in xyz order
                    out_chunk_idx = np.asarray([in_idx_0 // in_chunk_size[0],
                                                in_idx_1 // in_chunk_size[1],
                                                in_idx_2 // in_chunk_size[2]], dtype="uint32")[self.__axis_in_to_xyz]

                    print(f"{time.strftime("%H:%M:%S")} {(int(chunk_id / total_chunk_count * 100.)):02} % processing chunk "
                          f"x{out_chunk_idx[0]}y{out_chunk_idx[1]}z{out_chunk_idx[2]} [xyz] from "
                          f"{in_start} to {in_end} size {in_end - in_start} [{self.__data_set_cfg["axis_order"]}]", end='')

                    output_file = _output_dir / Path(output_name.format(out_chunk_idx[0],
                                                                       out_chunk_idx[1],
                                                                       out_chunk_idx[2],
                                                                       output_format))

                    if not output_file.exists():
                        # cur slice is a ZYX numpy array, as is expected by the writer
                        cur_slice = self.read_chunk(in_start, in_end)
                        converter.write_volume(cur_slice, str(output_file.resolve()), dtype="uint32",
                                               input_axis_order="zyx", apply_gzip=False)
                        print(" done.")
                    else:
                        print(" already exists. skipping.")

                    chunk_id += 1

        print("=============================")
        print(time.strftime("%H:%M:%S") + "  done")

        return out_chunk_count[0] - 1, out_chunk_count[1] - 1, out_chunk_count[2] - 1

