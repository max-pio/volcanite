from datetime import datetime
from pathlib import Path
from volcanite.volcaniteeval import VolcaniteArg, VolcaniteEvaluation, VolcaniteExec, VolcaniteLogFileCfg, ExistingPolicy

from common import data_specific_args

if __name__ == "__main__":

    # set up the evaluation output directory and the log files
    evaluation_name = Path(__file__).stem
    evaluation = VolcaniteEvaluation(eval_out_directory=f"./results/{evaluation_name}/", existing_policy=ExistingPolicy.APPEND,
                                        eval_name=evaluation_name,
                                        log_files=[VolcaniteLogFileCfg(f"{evaluation_name}.csv",
                                                              fmts=["{frame_min_ms},{frame_avg_ms},{frame_max_ms},{frame_sdv_ms},{frame_med_ms}"],
                                                              headers=["Data Set,Valid Requests Only,frame min [ms],frame avg [ms],frame max [ms],stdv,frame med [ms]"])],
                                        enable_log=True, dry_run=False)

    volcanite = VolcaniteExec(evaluation, build_subdir="cmake-build-release")
    volcanite.checkout_and_build()

    # print evaluation information to console
    print("\n" + volcanite.info_str())
    print("\n".join(volcanite.logs_info_str()))

    print("Data sets: " + ', '.join(sorted([args.identifier for args in VolcaniteArg.args_csgv_datasets.values()])))

    # log a time stamp
    evaluation.get_log().log_manual("# " + datetime.now().strftime("%Y.%m.%d-%H:%M:%S"))

    arg_video_cfg = VolcaniteArg.arg_video_eval_cfg(rotation=(-360, 0), zoom=(2, -0.2), duration=600, interpolant="smooth", edge=(0.2, 0.8),
                                                    duration_is_seconds=False, output_framerate=0)

    # iterate over all configuration combinations and execute Volcanite
    for arg_data in VolcaniteArg.args_csgv_datasets.values():

        if arg_data.identifier != "pa66" and arg_data.identifier.lower() != "motta2019-small" and arg_data.identifier.lower() != "h01-wm":
            continue

        # log data set name
        evaluation.get_log().log_manual("# " + arg_data.identifier + " -----------------")

        # load the .vcfg file for the data set (default perspective)
        arg_vcfg = VolcaniteArg.arg_config_import([arg_data])

        for arg_shaderdef in [VolcaniteArg([], "", 80),
                              VolcaniteArg(["--shader-def", "VALID_RAY_REQUEST_ONLY"], "valr", 80),
                              VolcaniteArg(["--shader-def", "COMPUTE_LOD_AT_RENDER_TIME"], "rlod", 80),
                              VolcaniteArg(["--shader-def", "VALID_RAY_REQUEST_ONLY,COMPUTE_LOD_AT_RENDER_TIME"], "valr-rlod", 80)]:

            arg_shading = VolcaniteArg.args_shading["ao"]

            # evaluate timings and export an image
            arg_video_export = VolcaniteArg.arg_video_export([arg_data, arg_shaderdef])

            arg_image_eval = VolcaniteArg.arg_image_eval_cfg(1024)

            vargs = [arg_data, arg_vcfg, arg_image_eval, arg_shading, arg_shaderdef] \
                        + data_specific_args(arg_data.identifier)
            
            # log a summary line of all arguments
            evaluation.get_log().log_manual("# " + VolcaniteArg.concat_arg_string(vargs))

            # the first two columns are written from the python script
            evaluation.get_log().log_manual(arg_data.identifier + "," + arg_shaderdef.identifier + ",", end="")
            # execute Volcanite and pass the Volcanite log file into which the results are appended
            volcanite.exec(vargs)

