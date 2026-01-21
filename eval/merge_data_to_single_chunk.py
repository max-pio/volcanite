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

from volcanite import converter as vc
from volcanite import converter_chunked as vcchunked
import argparse
from pathlib import Path

def merge_data_to_single_chunk(directory : Path):
    chunked_volumes = [("Ara2016/Ara2016_x{}y{}z{}.hdf5", (2, 1, 2), "Ara2016/Ara2016_full.hdf5"),
                       ("Griesser2022-sample/Griesser2022-sample_x{}y{}z{}.hdf5", (15,3,2), "Griesser2022-sample/Griesser2022-sample_full.hdf5"),
                       ("Griesser2022-validation/Griesser2022-validation_x{}y{}z{}.hdf5", (1,1,0), "Griesser2022-validation/Griesser2022-validation_full.hdf5"),
                       # H01-wm is too large (~2TB)
                       ("liconn/liconn_x{}y{}z{}.hdf5", (3,4,0), "liconn/liconn_full.hdf5"),
                       # Motta2019 is too large (~1TB)
                      ]

    for data_path, chunks_in, output_path in chunked_volumes:

        if (directory / Path(data_path.format(0, 0, 0))).exists():
            print(f"Reading volume {directory / Path(data_path)} ")
            volume = vcchunked.read_chunked_volume(str((directory / Path(data_path)).absolute()), chunks_in, "zyx")
            print(f"done. Size {volume.shape[2]}, {volume.shape[1]}, {volume.shape[0]}. ", end="")
            vc.write_volume(volume, output_path, dtype='uint32')
            print(f"Exported: {output_path}")
        else:
            print(f"Skipping {directory / Path(data_path)} (does not exist).")


if __name__ == '__main__':
    parser = argparse.ArgumentParser(
        prog='Segmentation Volume Merger',
        description='Merges segmentation volumes downloaded via download_cloud_data.py into single chunked hdf5 files.',
        epilog='')

    parser.add_argument("data_directory", metavar='data-directory', type=str, help="Directory where downloaded cloud data was stored.")
    args = parser.parse_args()
    merge_data_to_single_chunk(Path(args.data_directory))
