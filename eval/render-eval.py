from pathlib import Path
from volcanite.volcaniteeval import VolcaniteArg, VolcaniteEvaluation, VolcaniteExec, VolcaniteLogFileCfg, ExistingPolicy

def data_specific_args(data : str) -> list[VolcaniteArg]:
    vargs = []
    if data == "Motta2019" or data == "Griesser2022-sample" or data == "H01-wm" or data == "LICONN":
        vargs.append(VolcaniteArg("--cache-palette"))
        vargs.append(VolcaniteArg("--cache-size 2048"))
    else:
        vargs.append(VolcaniteArg("--cache-size 4095"))
    return vargs

if __name__ == "__main__":

    # set up the evaluation output directory and the log files
    evaluation = VolcaniteEvaluation(eval_out_directory="./results/render-eval/", existing_policy=ExistingPolicy.DELETE,
                                        eval_name="render-eval",
                                        log_files=[VolcaniteLogFileCfg("render-eval.csv",
                                                              fmts=["{name},{frame_min_ms},{frame_avg_ms},{frame_max_ms}"],
                                                              headers=["Name,frame min [ms],frame avg [ms],frame max [ms]"])],
                                        enable_log=True, dry_run=False)

    volcanite = VolcaniteExec(evaluation, build_subdir="cmake-build-release")
    volcanite.checkout_and_build()

    # print evaluation information to console
    print("\n" + volcanite.info_str())
    print("\n".join(volcanite.logs_info_str()))

    print("Data sets: " + ', '.join(sorted([args.identifier for args in VolcaniteArg.args_csgv_datasets.values()])))

    # constant arguments
    video_cfg = VolcaniteArg.arg_video_cfg(rotation=(-360, 0), zoom=(2, 0), duration=600, interpolant="smooth", edge=(0.2, 0.8),
                                              duration_is_seconds=False, output_framerate=0)

    # iterate over all configuration combinations and execute Volcanite
    for arg_data in VolcaniteArg.args_csgv_datasets.values():

        video_export = VolcaniteArg.arg_video_export([arg_data])
        render_config = VolcaniteArg.arg_config_import([arg_data])
        vargs = [arg_data, render_config, video_cfg, video_export] + data_specific_args(arg_data.identifier)

        # manually log a comment line to the log file
        evaluation.get_log().log_manual("# " + VolcaniteArg.concat_ids(vargs))

        # execute Volcanite and pass the Volcanite log file into which the results are appended
        volcanite.exec(vargs)

    # create a copy of the log file without comment lines that start with # which includes the #fmt: strings
    # evaluation.get_log().create_formatted_copy(evaluation.eval_out_directory / Path("results.csv"),
    #                                            remove_line_prefixes=["#"])

