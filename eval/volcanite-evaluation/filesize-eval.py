#  Copyright (C) 2026, Max Piochowiak, Karlsruhe Institute of Technology
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
import gzip
import lzma
import os
import struct
from datetime import datetime
from io import BytesIO
from pathlib import Path
from volcanite.volcaniteeval import VolcaniteArg, VolcaniteEvaluation, VolcaniteExec, VolcaniteLogFileCfg, ExistingPolicy

from common import data_specific_compression_args

GB_TO_BYTE = (1000 ** 3)
GiB_TO_BYTE = (1024 ** 3)

def get_dir_size_gb(path):
    """Return total size of directory in GB."""
    total = 0
    for dirpath, dirnames, filenames in os.walk(path):
        for f in filenames:
            total += os.path.getsize(os.path.join(dirpath, f))
    return total / GB_TO_BYTE

def get_dir_size_uncompressed_gb(path):
    """Return total size of directory in GB, using uncompressed size for .gz files."""
    total = 0
    for dirpath, dirnames, filenames in os.walk(path):
        for f in filenames:
            fp = os.path.join(dirpath, f)
            if f.endswith('.gz'):
                # Read last 8 bytes for gzip footer (skip CRC32, get ISIZE)
                with open(fp, 'rb') as file:
                    file.seek(-8, 2)  # From end
                    footer = file.read(8)
                    if len(footer) == 8:
                        isize = struct.unpack('<I', footer[-4:])[0]  # Little-endian uint32
                        total += isize
                    else:
                        total += os.path.getsize(fp)  # Fallback
            else:
                total += os.path.getsize(fp)
    return total / GB_TO_BYTE

if __name__ == "__main__":

    # set up the evaluation output directory and the log files
    evaluation_name = Path(__file__).stem

    with VolcaniteEvaluation(eval_out_directory=Path(f"./results/{evaluation_name}/"),
                             existing_policy=ExistingPolicy.DELETE, eval_name=evaluation_name,
                             log_files=[], enable_log=False, dry_run=True):

        print("Evaluating file sizes on disk: hdf5 (gzip), CSGV, CSGV (gzip).")

        Path(f"./results/{evaluation_name}/").mkdir(parents=True, exist_ok=True)
        print("Data sets: " + ', '.join(sorted([args.identifier for args in VolcaniteArg.args_csgv_datasets.values()])),
              end="\n\n")

        with open(f"./results/{evaluation_name}/{evaluation_name}.csv", "w") as f:
            f.write("# " + datetime.now().strftime("%Y.%m.%d-%H:%M:%S") + "\n")
            f.write("Data Set,hdf5 (gzip) Filesize [GB],CSGV Filesize [GB],CSGV (gzip) Filesize [GB],CSGV (lzma) Filesize [GB]\n")

            for arg_data in VolcaniteArg.args_csgv_datasets.values():
                arg_input = data_specific_compression_args(arg_data.identifier,
                                                           volume_data_dir=VolcaniteArg.get_csgv_directory(),
                                                           brick_size=False, operations=False)[0].args

                # gzip compressed hdf5 files
                input_files = []
                if arg_input[0] == "--chunked":
                    last_chunk = [int(v) for v in arg_input[1].split(",")]
                    for z in range(0, last_chunk[2] + 1):
                        for y in range(0, last_chunk[1] + 1):
                            for x in range(0, last_chunk[0] + 1):
                                input_files.append(arg_input[2].format(x, y, z))
                else:
                    input_files = [Path(arg_input[0])]
                size_hdf5 = sum([os.path.getsize(os.path.join(file)) / GB_TO_BYTE for file in input_files])

                # csgv file
                csgv_file = Path(arg_data.args[0])
                size_csgv = os.path.getsize(os.path.join(csgv_file)) / GB_TO_BYTE

                # gzip compressed csgv file
                output = BytesIO()
                with csgv_file.open('rb') as f_in, gzip.GzipFile(fileobj=output, mode='wb') as f_out:
                    # Stream copy (shutil.copyfileobj works too)
                    while chunk := f_in.read(8192):
                        f_out.write(chunk)
                size_csgv_gzip = output.tell() / GB_TO_BYTE

                # LZMA file size
                output = BytesIO()
                with csgv_file.open('rb') as f_in, lzma.LZMAFile(output, mode='wb') as f_out:
                    while chunk := f_in.read(8192):
                        f_out.write(chunk)
                size_csgv_lzma = output.tell() / GB_TO_BYTE

                f.write(f"{arg_data.identifier},{size_hdf5},{size_csgv},{size_csgv_gzip},{size_csgv_lzma}\n")
