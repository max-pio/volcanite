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
from argparse import ArgumentError

from volcanite import converter as vc

if __name__ == '__main__':
    parser = argparse.ArgumentParser(
        prog='Segmentation Volume Converter',
        description='Converts between different segmentation volume file formats.',
        epilog='')

    parser.add_argument('input_file', help='path to input volume file')
    parser.add_argument('output_file', help='path to output volume file')
    parser.add_argument('-z', '--gzip', action='store_true', help="apply additional gzip compression on output file")
    parser.add_argument('--vis', action='store_true', help="show a 2D plot of volume slices after import")
    parser.add_argument('-v', '--verbose', action='store_true', help="enable verbose output")
    parser.add_argument('--chunked_in', type=int, nargs=3, help="maximum existing XYZ chunk indices for the input chunks")
    parser.add_argument('--chunked_out', type=int, nargs=3, help="maximum existing XYZ chunk indices for the output chunks")

    args = parser.parse_args()

    # TODO: add arguments to convert chunked files
    if args.chunked_in:
        raise ArgumentError(args.chunked_in, message="Chunked volume conversion not yet implemented.")
    elif args.chunked_out:
        raise ArgumentError(args.chunked_out, message="Chunked volume conversion not yet implemented.")

    volume = vc.read_volume(args.input_file)

    if args.verbose:
        vc.debug_print(volume)
    if args.vis:
        vc.debug_vis(volume)

    if args.output_file:
        vc.write_volume(volume, args.output_file, dtype='uint32', apply_gzip=args.gzip)
    exit(0)
