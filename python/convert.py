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
from volcanite import converter_chunked as vcchunked

if __name__ == '__main__':
    parser = argparse.ArgumentParser(
        prog='Segmentation Volume Converter',
        description='Converts between different segmentation volume file formats.',
        epilog='')

    parser.add_argument('input_file', metavar='input-file', help='path to input volume file')
    parser.add_argument('output_file', metavar='output-file', help='path to output volume file')
    parser.add_argument('-z', '--gzip', action='store_true', help="apply additional gzip compression on output file")
    parser.add_argument('--vis', action='store_true', help="show a 2D plot of volume slices after import")
    parser.add_argument('-v', '--verbose', action='store_true', help="enable verbose output")
    parser.add_argument('--chunk-in', type=int, nargs=3, help="Last XYZ chunk indices for the input chunks. Requires formatted input-file.")
    parser.add_argument('--chunk-size-out', type=int, nargs=3, help="maximum existing XYZ chunk indices for the output chunks")
    parser.add_argument('--axes', help="axis order of volume file (default ZYX)")

    args = parser.parse_args()

    # safeguards
    if args.chunk_size_out and not args.output_file:
        parser.error("Must provide --output-file with chunked output")

    # special case for chunked input and output: file streaming does not store full volume in memory.
    if args.chunk_in and args.chunk_size_out:
        if len(args.chunk_in) != 3:
            parser.error(f"--chunk-in X Y Z must have three indices but got {len(args.chunk_in)}.")
        if len(args.chunk_size_out) != 3:
            parser.error(f"--chunk-size-out W H D must have three dimensions but got {len(args.chunk_size_out)}.")
        if args.verbose or args.vis:
            parser.error(f"--verbose or --vis are not supported for chunked input to chunked output volumes.")

        # file streamed conversion
        vcchunked.convert_chunked_volume(args.input_file,
                                         last_input_chunk_xyz=args.chunk_in,
                                         path_out_format=args.output_file, chunk_size_out=args.chunk_size_out,
                                         thread_count=16, dtype_out='uint32')
    # other cases: full volume is read into RAM first
    else:
        # chunked input
        if args.chunk_in and not args.chunk_size_out:
            if len(args.chunk_in) != 3:
                parser.error(f"--chunk-in X Y Z must have three indices but got {len(args.chunk_in)}.")
            volume = vcchunked.read_chunked_volume(args.input_file, args.chunk_in, args.axes.lower() if args.axes else "ZYX")
        else:
            volume = vc.read_volume(args.input_file, args.axes.lower() if args.axes else "ZYX")

        if args.verbose:
            vc.debug_print(volume)
        if args.vis:
            vc.debug_vis(volume)

        # chunked output
        if args.output_file:
            if not args.chunk_in and args.chunk_size_out:
                if len(args.chunk_size_out) != 3:
                    parser.error(f"--chunk-size-out W H D must have three dimensions but got {len(args.chunk_size_out)}.")

                vcchunked.write_chunked_volume(volume, args.output_file, chunk_size=args.chunk_size_out,
                                               dtype='uint32', apply_gzip=args.gzip)
            else:
                vc.write_volume(volume, args.output_file, dtype='uint32', apply_gzip=args.gzip)


    exit(0)
