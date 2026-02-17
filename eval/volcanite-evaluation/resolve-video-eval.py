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
                                                              fmts=["{frame_min_ms},{frame_avg_ms},{frame_max_ms},{frame_sdv_ms},{frame_med_ms},"
                                                                    "{frame_gpu_cache_min_ms},{frame_gpu_decomp_min_ms},{frame_gpu_render_min_ms},{frame_gpu_post_min_ms},"
                                                                    "{frame_gpu_cache_avg_ms},{frame_gpu_decomp_avg_ms},{frame_gpu_render_avg_ms},{frame_gpu_post_avg_ms},"
                                                                    "{frame_gpu_cache_max_ms},{frame_gpu_decomp_max_ms},{frame_gpu_render_max_ms},{frame_gpu_post_max_ms},"
                                                                     + ",".join("{{frame_{:02}_ms}}".format(i) for i in range(16))],
                                                              headers=["Data Set,Denoising,Subsampling,"
                                                                       "frame min [ms],frame avg [ms],frame max [ms],frame stdv [ms],frame med [ms],"
                                                                       "cache min [ms],decompress min [ms],render min [ms],post-process min [ms],"
                                                                       "cache avg [ms],decompress avg [ms],render avg [ms],post-process avg [ms],"
                                                                       "cache max [ms],decompress max [ms],render max [ms],post-process max [ms],"
                                                                        + ",".join("Frame {:02}".format(i) for i in range(16))])],
                                        enable_log=True, dry_run=False) as evaluation:

        volcanite = VolcaniteExec(evaluation, build_subdir="cmake-build-release")
        volcanite.checkout_and_build()

        # print evaluation information to console
        print("\n" + volcanite.info_str())
        print("\n".join(volcanite.logs_info_str()))

        print("Data sets: " + ', '.join(sorted([args.identifier for args in VolcaniteArg.args_csgv_datasets.values()])), end="\n\n")

        # log a time stamp
        evaluation.get_log().log_manual("# " + datetime.now().strftime("%Y.%m.%d-%H:%M:%S"))


        # constant arguments
        arg_video_cfg = VolcaniteArg.arg_video_eval_cfg(rotation=(-360, 0), zoom=(2, -0.2), duration=600, interpolant="smooth", edge=(0.2, 0.8),
                                                duration_is_seconds=False, output_framerate=0)

        arg_shading = VolcaniteArg.args_shading["pt"]

        # iterate over all configuration combinations and execute Volcanite
        for arg_data in VolcaniteArg.args_csgv_datasets.values():

            # log data set name
            evaluation.get_log().log_manual("# " + arg_data.identifier + " -----------------")

            # load the .vcfg file for the data set (default perspective)
            arg_vcfg = VolcaniteArg.arg_config_import([arg_data])

            for arg_denoising in [VolcaniteArg(["--config", "[Rendering] Denoising: 0"], "", 176),
                                  VolcaniteArg(["--config", "[Rendering] Denoising: 1"], "dns", 176)]:


                for arg_subsampling in [VolcaniteArg(["--config", "[Display] Resolution_Subsampling: 0"], "subs0", 177),
                                        VolcaniteArg(["--config", "[Display] Resolution_Subsampling: 1"], "subs1", 177),
                                        VolcaniteArg(["--config", "[Display] Resolution_Subsampling: 2"], "subs2", 177)]:

                    # evaluate timings and export path as video
                    arg_video_export = VolcaniteArg.arg_video_export([arg_data, arg_denoising, arg_subsampling])
                    arg_timing_export = VolcaniteArg.arg_timing_export([arg_data, arg_denoising, arg_subsampling])

                    vargs = [arg_data, arg_vcfg, arg_shading, arg_denoising, arg_subsampling, arg_timing_export, arg_video_cfg, arg_video_export] + data_specific_rendering_args(arg_data.identifier)

                    # log a summary line of all arguments
                    evaluation.get_log().log_manual("# " + VolcaniteArg.concat_ids(vargs))

                    # the first two columns are written from the python script
                    subsampl_str = ["1", "1/4", "1/16", "1/64"]
                    evaluation.get_log().log_manual(arg_data.identifier + "," + ("y" if arg_denoising.identifier else "n") + "," + subsampl_str[int(arg_subsampling.identifier[4])] + ",", end="")
                    # execute Volcanite and pass the Volcanite log file into which the results are appended
                    volcanite.exec(vargs)


        # create_formatted_copy(evaluation.eval_out_directory / Path("image-eval.csv"), newline_separator="\\\\")
