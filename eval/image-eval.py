from datetime import datetime
from pathlib import Path
from volcanite.volcaniteeval import VolcaniteArg, VolcaniteEvaluation, VolcaniteExec, VolcaniteLogFileCfg, ExistingPolicy

def data_specific_args(data : str) -> list[VolcaniteArg]:
    vargs = []
    if data == "Motta2019" or data == "Griesser2022-sample" or data == "H01-wm":
        vargs.append(VolcaniteArg("--cache-palette"))
        if data == "Motta2019" or data == "H01-wm":
            vargs.append(VolcaniteArg("--cache-size 1024"))
        else:
            vargs.append(VolcaniteArg("--cache-size 2048"))
        if data =="H01-wm":
            vargs.append(VolcaniteArg("--stream-lod"))
    else:
        vargs.append(VolcaniteArg("--cache-size 4095"))
    return vargs

if __name__ == "__main__":

    # set up the evaluation output directory and the log files
    evaluation_name = Path(__file__).stem
    evaluation = VolcaniteEvaluation(eval_out_directory=f"./results/{evaluation_name}/", existing_policy=ExistingPolicy.DELETE,
                                        eval_name=evaluation_name,
                                        log_files=[VolcaniteLogFileCfg(f"{evaluation_name}.csv",
                                                              fmts=["{frame_min_ms},{frame_avg_ms},{frame_max_ms},{frame_sdv_ms},{frame_med_ms},"
                                                                     + ",".join("{{frame_{:02}_ms}}".format(i) for i in range(16))],
                                                              headers=["Data Set,Shading Mode,frame min [ms],frame avg [ms],frame max [ms],stdv,frame med [ms],"
                                                                        + ",".join("Frame {:02}".format(i) for i in range(16))])],
                                        enable_log=True, dry_run=False)

    volcanite = VolcaniteExec(evaluation, build_subdir="cmake-build-release")
    volcanite.checkout_and_build()

    # print evaluation information to console
    print("\n" + volcanite.info_str())
    print("\n".join(volcanite.logs_info_str()))

    print("Data sets: " + ', '.join(sorted([args.identifier for args in VolcaniteArg.args_csgv_datasets.values()])))

    # log a time stamp
    evaluation.get_log().log_manual("# " + datetime.now().strftime("%Y.%m.%d-%H:%M:%S"))

    # iterate over all configuration combinations and execute Volcanite
    for arg_data in VolcaniteArg.args_csgv_datasets.values():

        # log data set name
        evaluation.get_log().log_manual("# " + arg_data.identifier + " -----------------")

        # load the .vcfg file for the data set (default perspective)
        arg_vcfg = VolcaniteArg.arg_config_import([arg_data])

        for arg_shading in VolcaniteArg.args_shading.values():

            # evaluate timings and export an image
            arg_image_eval = VolcaniteArg.arg_image_eval_cfg(1024)
            arg_image_export = VolcaniteArg.arg_image_export([arg_data, arg_shading])

            vargs = [arg_data, arg_vcfg, arg_shading, arg_image_eval, arg_image_export] + data_specific_args(arg_data.identifier)

            # log a summary line of all arguments
            evaluation.get_log().log_manual("# " + VolcaniteArg.concat_arg_string(vargs))

            # the first two columns are written from the python script
            evaluation.get_log().log_manual(arg_data.identifier + "," + arg_shading.identifier + ",", end="")
            # execute Volcanite and pass the Volcanite log file into which the results are appended
            volcanite.exec(vargs)


    # create a copy of the log file with correct formatting
    # evaluation.get_log().create_formatted_copy(evaluation.eval_out_directory / Path("image-eval.csv"), newline_separator="\\\\")

