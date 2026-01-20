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

if __name__ == '__main__':
    parser = argparse.ArgumentParser(
        prog='Segmentation Volume Merger',
        description='Merges segmentation volumes downloaded via download_cloud_data.py into single chunked hdf5 files.',
        epilog='')

    parser.add_argument("data_directory", metavar='data-directory', type=str, help="Directory where downloaded cloud data was stored.")
    parser.add_argument("-v", "--verbose", action="store_true")

    args = parser.parse_args()
    directory = Path(args.data_directory)

    chunked_volumes = [("Ara2016/Ara2016_x{}y{}z{}.hdf5", (2,1,2), "Ara2016/Ara2016_full.hdf5"),]

    for data_path, chunks_in, output_path in chunked_volumes:

        if (directory / Path(data_path.format(0, 0, 0))).exists():
            print(f"Reading volume {directory / Path(data_path)} ")
            volume = vcchunked.read_chunked_volume(str((directory / Path(data_path)).absolute()), chunks_in, "zyx")
            print(f"done. Size {volume.shape[2]}, {volume.shape[1]}, {volume.shape[0]}. ", end="")
            vc.write_volume(volume, output_path, dtype='uint32')
            print(f"Exported: {output_path}")
        else:
            print(f"Skipping {directory / Path(data_path)} (does not exist).")
