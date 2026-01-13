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
                                                              fmts=["{orig_gb},{mem_total_mb},{mem_encoding_mb},{mem_cache_mb},{mem_cache_fillrate_pcnt},{mem_materials_mb},{mem_cache_packing_factor}"],
                                                              headers=["Data Set,Uncompressed [GB],Total VRAM [MB],Compressed Encoding [MB],Cache [MB],Cache Usage [Pcnt],Attributes / Materials [MB],Cache Packing Factor"])],
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

            # evaluate with fixed shading mode: shadow rays
            arg_shading = VolcaniteArg.args_shading["shadow"]

            # evaluate timings and export an image
            arg_image_eval = VolcaniteArg.arg_image_eval_cfg(1024)

            # log a summary line of all arguments
            vargs = [arg_data, arg_vcfg, arg_shading, arg_image_eval, VolcaniteArg(["--verbose"])] + data_specific_rendering_args(arg_data.identifier)
            evaluation.get_log().log_manual("# " + VolcaniteArg.concat_arg_string(vargs))

            # write data set name to csv logfile
            evaluation.get_log().log_manual(arg_data.identifier + ",", end="")
            # execute Volcanite and pass the Volcanite log file into which the results are appended
            volcanite.exec(vargs)

