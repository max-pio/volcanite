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

from common import data_specific_compression_args, data_specific_rendering_args


# This evaluation is separate from csgv-eval.py for these reasons:
# - it re-compresses all data for compression timings (therefore requiring the raw data not just csgv files)
# - it measures time to first frame (incl. compression), therefore --stream-lod is only applied to relevant data

if __name__ == "__main__":

    # set up the evaluation output directory and the log files
    evaluation_name = Path(__file__).stem
    with VolcaniteEvaluation(eval_out_directory=f"./results/{evaluation_name}/", existing_policy=ExistingPolicy.DELETE,
                                        eval_name=evaluation_name,
                                        log_files=[VolcaniteLogFileCfg(f"{evaluation_name}.csv",
                                                              fmts=["{volume_dim_x},{volume_dim_y},{volume_dim_z},{volume_labels},{orig_gb},{orig_bits_per_voxel},"
                                                                    "{csgv_gb},{csgv_bits_per_voxel},{comprate_pcnt},{csgv_detail_gb},{csgv_detail_pcnt},{brick_size},{operation_mask},"
                                                                    "{comp_prepass_s},{comp_mainpass_s},{comp_s},{comp_with_fileio_s},{time_to_first_frame_s},"
                                                                    "{brick_palette_size_min},{brick_palette_size_avg},{brick_palette_size_max},"
                                                                    "{brick_palette_duplicates_min},{brick_palette_duplicates_avg},{brick_palette_duplicates_max},"
                                                                    "{brick_labels_min},{brick_labels_avg},{brick_labels_max}"],
                                                              headers=["Data Set,DimX,DimY,DimZ,Labels,Orig Size [GB],Orig bits/voxel,"
                                                                       "CSGV Size [GB],CSGV bits/voxel,Compression Rate [Pcnt],Detail Encoding Size [GB],Detail Encoding [Pcnt],Brick Size,Operations,"
                                                                       "Compression Time Prepass [s],Compression Main [s],Compression Time Total [s],Compression Time Total with IO [s],Time To First Frame [s],"
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
        # (precomputed csgv files are not actually loaded)
        for arg_csgv in VolcaniteArg.args_csgv_datasets.values():

            # the raw input data and compression parameters for the compression.
            # input data sets are assumed to be structured in subdirectories in the same location as the csgv files,
            # as created by the download_evaluation_data.py download script.
            args_data_input = data_specific_compression_args(arg_csgv.identifier,
                                                             volume_data_dir=VolcaniteArg.get_csgv_directory())

            # chunked data sets require a compression export path:
            if any("--chunked" in a.args for a in args_data_input):
                decompression_path = Path(f"./results/{evaluation_name}/{VolcaniteArg.concat_ids([arg_csgv])}.csgv")
                args_data_input.append(VolcaniteArg(["-c", str(decompression_path.resolve())]))
            else:
                decompression_path = None

            # use the default rendering parameters for each data set (req. to obtain time-to-first-frame)
            args_rendering = data_specific_rendering_args(arg_csgv.identifier)

            # the first column is written from the python script
            evaluation.get_log().log_manual(arg_csgv.identifier + "," , end="")
            # execute Volcanite and pass the Volcanite log file into which the results are appended
            volcanite.exec(args_data_input + args_rendering)

            # remove any large cmpressed files
            if decompression_path:
                decompression_path.unlink()