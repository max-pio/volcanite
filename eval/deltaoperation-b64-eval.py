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

if __name__ == "__main__":

    # set up the evaluation output directory and the log files
    evaluation_name = Path(__file__).stem
    with VolcaniteEvaluation(eval_out_directory=f"./results/{evaluation_name}/", existing_policy=ExistingPolicy.DELETE,
                                        eval_name=evaluation_name,
                                        log_files=[VolcaniteLogFileCfg(f"{evaluation_name}.csv",
                                                              fmts=["{operation_mask},{volume_labels},{orig_gb},{orig_bits_per_voxel},"
                                                                    "{csgv_gb},{csgv_bits_per_voxel},{comprate_pcnt},{csgv_detail_gb},{csgv_detail_pcnt},{brick_size},"
                                                                    "{brick_palette_size_min},{brick_palette_size_avg},{brick_palette_size_max},"
                                                                    "{brick_palette_duplicates_min},{brick_palette_duplicates_avg},{brick_palette_duplicates_max},"
                                                                    "{brick_labels_min},{brick_labels_avg},{brick_labels_max},"
                                                                    "{mem_cache_mb},{mem_cache_used_mb},{mem_cache_fillrate_pcnt},{mem_cache_packing_factor}"],
                                                              headers=["Data Set,Unlimited Pdelta,Operations,Labels,Orig Size [GB],Orig bits/voxel,"
                                                                       "CSGV Size [GB],CSGV bits/voxel,Compression Rate [Pcnt],Detail Encoding Size [GB],Detail Encoding [Pcnt],Brick Size,"
                                                                       "Palette Length min,Palette Length avg,Palette Length max,"
                                                                       "Palette Duplicates min,Palette Duplicates avg,Palette Duplicates max,"
                                                                       "Brick Labels min,Brick Labels avg,Brick Labels max,"
                                                                       "Cache Size [MB],Used Size [MB],Used Size [Pcnt],Packing Factor"])],
                                        enable_log=True, dry_run=False) as evaluation:

        volcanite = VolcaniteExec(evaluation, build_subdir="cmake-build-release")
        volcanite.checkout_and_build()

        # print evaluation information to console
        print("\n" + volcanite.info_str())
        print("\n".join(volcanite.logs_info_str()))

        print("Data sets: " + ', '.join(sorted([args.identifier for args in VolcaniteArg.args_csgv_datasets.values()])), end="\n\n")

        # log a time stamp
        evaluation.get_log().log_manual("# " + datetime.now().strftime("%Y.%m.%d-%H:%M:%S"))

        # iterate over all configuration combinations and execute Volcanite to re-compress the data:
        for arg_csgv in VolcaniteArg.args_csgv_datasets.values():

            # compress three times: without any delta, with the old (1 < delta < 17) and once with the new (unlimited) palette delta
            for arg_unlim_pdelta in ["", "d-", "d"]:

                # some data sets are omitted in the plots
                if arg_csgv.identifier in ["pa66", "Griesser2022-sample", "xtm-battery", "H01-bloodvessel", "Griesser2022-validation"]:
                    # instead of skipping the computation, they are commented out in the .csv
                    # continue
                    evaluation.get_log().log_manual("#", end="")

                # the first two columns are written from the python script
                evaluation.get_log().log_manual(arg_csgv.identifier + "," + arg_unlim_pdelta + ",", end="")

                # the raw input data and compression parameters (except operation list) for the compression
                # input data sets are assumed to be structured in subdirectories in the same location as the csgv files, 
                # as created by the download_evaluation_data.py download script.
                args_data_input = data_specific_compression_args(arg_csgv.identifier, volume_data_dir=VolcaniteArg.get_csgv_directory(), operations=False, brick_size=False)

                # the old palette delta operation behavior had a limited delta length. enable with 'p-' operation
                arg_operation = VolcaniteArg.arg_operations("pnl" + arg_unlim_pdelta + "s")

                args_rendering = data_specific_rendering_args(arg_csgv.identifier, cache_palette=False, stream_lod=False)

                # chunked data must have a decompression path
                arg_csgv_export = VolcaniteArg(["-c", str(Path(f"./results/{evaluation_name}/{VolcaniteArg.concat_ids([arg_csgv, arg_operation])}.csgv").resolve())])

                # execute Volcanite and pass the Volcanite log file into which the results are appended
                # stream-lod is necessary to force detail separation (and obtain the detail encoding size)
                # cache-palette is enabled to obtain cache packing factors
                volcanite.exec(args_data_input + args_rendering + [VolcaniteArg.args_brick_size["64"], arg_operation, arg_csgv_export, VolcaniteArg("--stream-lod"), VolcaniteArg("--cache-palette")])
