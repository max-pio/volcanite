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

from datetime import datetime
from pathlib import Path
from volcanite.volcaniteeval import VolcaniteArg, VolcaniteEvaluation, VolcaniteExec, VolcaniteLogFileCfg, ExistingPolicy

from common import data_specific_rendering_args

if __name__ == "__main__":

    # set up the evaluation output directory and the log files
    evaluation_name = Path(__file__).stem
    with VolcaniteEvaluation(eval_out_directory=f"./results/{evaluation_name}/", existing_policy=ExistingPolicy.DELETE,
                                        eval_name=evaluation_name,
                                        log_files=[VolcaniteLogFileCfg(f"{evaluation_name}.csv",
                                                              fmts=["{volume_dim_x},{volume_dim_y},{volume_dim_z},{volume_labels},{orig_gb},{orig_bits_per_voxel},"
                                                                    "{csgv_gb},{csgv_bits_per_voxel},{comprate_pcnt},{csgv_detail_gb},{csgv_detail_pcnt},{brick_size},{operation_mask},"
                                                                    "{brick_palette_size_min},{brick_palette_size_avg},{brick_palette_size_max},"
                                                                    "{brick_palette_duplicates_min},{brick_palette_duplicates_avg},{brick_palette_duplicates_max},"
                                                                    "{brick_labels_min},{brick_labels_avg},{brick_labels_max}"],
                                                              headers=["Data Set,DimX,DimY,DimZ,Labels,Orig Size [GB],Orig bits/voxel,"
                                                                       "CSGV Size [GB],CSGV bits/voxel,Compression Rate [Pcnt],Detail Encoding Size [GB],Detail Encoding [Pcnt],Brick Size,Operations,"
                                                                       "Palette Length min,Palette Length avg,Palette Length max,"
                                                                       "Palette Duplicates min,Palette Duplicates avg,Palette Duplicates max,"
                                                                       "Brick Labels min,Brick Labels avg,Brick Labels max"])],
                                        enable_log=True, dry_run=False) as evaluation:

        volcanite = VolcaniteExec(evaluation, build_subdir="cmake-build-release")
        volcanite.checkout_and_build()

        # print evaluation information to console
        print("\n" + volcanite.info_str())
        print("\n".join(volcanite.logs_info_str()))

        print("Data sets: " + ', '.join(sorted([args.identifier for args in VolcaniteArg.args_csgv_datasets.values()])), end="\n\n")

        # log a time stamp
        evaluation.get_log().log_manual("# " + datetime.now().strftime("%Y.%m.%d-%H:%M:%S"))

        # iterate over all configuration combinations and execute Volcanite
        for arg_data in VolcaniteArg.args_csgv_datasets.values():

            # the first column is written from the python script
            evaluation.get_log().log_manual(arg_data.identifier + "," , end="")
            # execute Volcanite and pass the Volcanite log file into which the results are appended
            # stream-lod is necessary to force detail separation (and obtain the detail encoding size)
            volcanite.exec([arg_data, VolcaniteArg("--stream-lod"), VolcaniteArg("--verbose")])

