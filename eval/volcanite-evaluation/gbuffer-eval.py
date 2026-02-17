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
    with VolcaniteEvaluation(eval_out_directory=f"./results/{evaluation_name}/", existing_policy=ExistingPolicy.APPEND,
                                        eval_name=evaluation_name,
                                        log_files=[], enable_log=False, dry_run=False) as evaluation:

        volcanite = VolcaniteExec(evaluation, build_subdir="cmake-build-release")
        volcanite.checkout_and_build()

        # print evaluation information to console
        print("\n" + volcanite.info_str())
        print("\n".join(volcanite.logs_info_str()))

        print("Data sets: " + ', '.join(sorted([args.identifier for args in VolcaniteArg.args_csgv_datasets.values()])),
              end="\n\n")


        # constant arguments
        arg_shading = VolcaniteArg.args_shading["local"]
        arg_debug_view = VolcaniteArg(["--dev",
                                       "--config", "[Development] Debug_View: 128",
                                       "--config", "[Development] Every_Frame##framebuffer: 1",
                                       "--config", "[Development] Constant_Mouse_Pos: 1",])

        # iterate over all configuration combinations and execute Volcanite
        for arg_data in VolcaniteArg.args_csgv_datasets.values():

            if arg_data.identifier != "cells":
                continue

            # load the .vcfg file for the data set (default perspective)
            arg_vcfg = VolcaniteArg.arg_config_import([arg_data])

            # create full-res G-buffer images
            for arg_gbuffer in [VolcaniteArg(["--config", "[Development] Mouse_XY: 0.0 1.0"], "depth", 189),
                                VolcaniteArg(["--config", "[Development] Mouse_XY: 1.0 0.0"], "normal", 189),
                                VolcaniteArg(["--config", "[Development] Mouse_XY: 0.0 0.0"], "label", 189),
                                VolcaniteArg(["--config", "[Development] Mouse_XY: 1.0 1.0"], "albedo", 189)]:


                arg_image_export = VolcaniteArg.arg_image_export([arg_data, arg_gbuffer])

                vargs = [arg_data, arg_vcfg, arg_shading, arg_image_export,
                         arg_debug_view, arg_gbuffer] + data_specific_rendering_args(arg_data.identifier)

                # execute Volcanite to render the images
                volcanite.exec(vargs)
