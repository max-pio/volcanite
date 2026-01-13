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
                                                              fmts=["{mem_cache_mb},{mem_cache_used_mb},{mem_cache_fillrate_pcnt},{mem_cache_packing_factor},{frame_min_ms},{frame_avg_ms},{frame_max_ms},{frame_sdv_ms},{frame_med_ms},{min_spp},{max_spp}"],
                                                              headers=["Data Set,Shading Mode,Cache Palette,Cache Size [MB],Used Size [MB],Used Size [Pcnt],Packing Factor,frame min [ms],frame avg [ms],frame max [ms],stdv,frame med [ms],SPP min,SPP max"])],
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

            # log data set name
            evaluation.get_log().log_manual("# " + arg_data.identifier + " -----------------")

            # load the .vcfg file for the data set (default perspective)
            arg_vcfg = VolcaniteArg.arg_config_import([arg_data])

            for arg_shading in VolcaniteArg.args_shading.values():

                for arg_mode in [VolcaniteArg([], "", 170),
                                 VolcaniteArg(["--cache-palette"], "cshpal", 170)]:

                    # evaluate timings and export an image
                    arg_image_eval = VolcaniteArg.arg_image_eval_cfg(1024)
                    arg_image_export = VolcaniteArg.arg_image_export([arg_data, arg_shading, arg_mode])
                    arg_timing_export = VolcaniteArg.arg_timing_export([arg_data, arg_shading, arg_mode])

                    vargs = [arg_data, arg_vcfg, arg_shading, arg_mode, \
                             arg_timing_export, arg_image_eval, arg_image_export] \
                            + data_specific_rendering_args(arg_data.identifier, cache_palette=False)

                    # log a summary line of all arguments
                    evaluation.get_log().log_manual("# " + VolcaniteArg.concat_arg_string(vargs))

                    # the first columns are written from the python script
                    evaluation.get_log().log_manual(arg_data.identifier + "," + arg_shading.identifier + "," + ("yes" if arg_mode.identifier == "cshpal" else "no") + ",", end="")
                    # execute Volcanite and pass the Volcanite log file into which the results are appended
                    volcanite.exec(vargs)

