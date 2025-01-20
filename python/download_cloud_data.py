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

from volcanite import clouddata as vcd
import argparse

if __name__ == '__main__':
    parser = argparse.ArgumentParser(
        prog='Segmentation Volume Downloader',
        description='Downloads segmentation volumes from cloud storages and stores them locally.',
        epilog='')

    parser.add_argument("dataset", help="data set url or example name. 'list-examples' lists available names.")
    parser.add_argument("-d", "--directory", help="empty/non-existing directory where data is stored.")
    parser.add_argument("-s", "--size", type=int, nargs=3, help="size of downloaded volume in voxels (default: full volume).")
    parser.add_argument("-f", "--filetype", default="hdf5", help="file type in which chunks are stored.")
    parser.add_argument("-o", "--origin", type=int, nargs=3, help="origin of the sub-volume in the full data set.")
    parser.add_argument("--axis-order", default="xyz", help="axis order of the volume: a permutation of XYZ (default XYZ). exported chunks always have XYZ order.")
    parser.add_argument("-c", "--chunk_size", type=int, nargs=3, default=(1024,1024,1024), help="volume is split into XYZ chunks of this size. should be dividable by 64.")
    parser.add_argument("-a", "--append", action="store_true", default=False, help="ignore non-empty output directory and skip existing chunk files.")
    parser.add_argument("-n", "--name", help="file name prefix for chunks that will be extended to [name]_x{}y{}z{}.[filetype]")
    parser.add_argument("-v", "--verbose", action="store_true")

    args = parser.parse_args()

    example_data = {"h01": ("gs://h01-release/data/20210601/c3/", {"axis_order": "xyz"}),
                    "witvliet2020": ("bossdb://witvliet2020/Dataset_8/segmentation", {"axis_order": "zyx"}),
                    "ara2016": ("bossdb://ara_2016/sagittal_10um/annotation_10um_2017", {"axis_order": "zyx"}),
                    }
    if args.dataset == "list-examples":
        print("Available short keys for dataset argument:\n  " + "\n  ".join(example_data.keys()))
        exit(0)
    data_set_url, data_set_cfg = example_data[args.dataset] if args.dataset in example_data else (args.dataset, {"axis_order": args.axis_order})

    if not args.name is None:
        if not args.name:
            output_name = ""
        else:
            output_name = args.name + "_"
    elif args.dataset in example_data:
        output_name = args.dataset + "_"
    else:
        output_name = ""
    output_name = output_name + "x{}y{}z{}.{}"

    # obtain data set
    data = vcd.CloudDataDownload(data_set_url, data_set_cfg=data_set_cfg)

    # to download and visualize one small chunk
    # converter.debug_vis(data.read_chunk(data.get_shape() // 2 - (200, 100, 16), (400, 200, 32)))

    # if no output directory is given, only print the shape of the volume if it is accessible
    if args.directory is None:
        print(f"Volume {data_set_url} is available with size {data.get_shape()}, assuming axis order"
              f" {data.get_download_axis_order()}. Specify a download directory with -d /path/to/dir/")
        exit(0)
    else:
        data.download(output_dir=args.directory, output_name=output_name, output_format=args.filetype,
                      volume_size=args.size, origin=args.origin,
                      chunk_size=args.chunk_size, continue_download=args.append)
        exit(0)
