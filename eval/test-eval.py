from pathlib import Path
import volcanite.volcaniteeval as ve

if __name__ == "__main__":
    # set up the evaluation output directory and the log files
    evaluation = ve.VolcaniteEvaluation(eval_out_directory="./my_test_eval", existing_policy=ve.ExistingPolicy.DELETE,
                                        eval_name="my_test_eval",
                                        log_files=[ve.VolcaniteLogFileCfg("results.txt",
                                                              fmts=["{name},{comprate_pcnt:.3},{frame_avg_ms}"],
                                                              headers=["Name,Compression Rate [%],frame avg [ms]"])],
                                        enable_log=True, dry_run=False)

    volcanite = ve.VolcaniteExec(evaluation, build_subdir="cmake-build-release")
    volcanite.checkout_and_build()

    # print evaluation information to console
    print(volcanite.info_str())
    print('\n'.join(volcanite.logs_info_str()))

    # iterate over all configuration combinations and execute Volcanite
    for arg_data in ve.VolcaniteArg.args_datasynth.values():

        # combine several VolcaniteArgs
        vargs = [arg_data, ve.VolcaniteArg.args_encoding["rANS"]]
        # add an image export arg that uses the IDs of the previous args as file names
        vargs.append(ve.VolcaniteArg.arg_image_export(vargs))

        # manually log a comment line to the log file
        evaluation.get_log().log_manual("#Running evaluation: " + ve.VolcaniteArg.concat_ids(vargs))

        # execute Volcanite and pass the Volcanite log file
        volcanite.exec(vargs)

    # create a copy of the log file without comment lines that start with # which includes the #fmt: strings
    evaluation.get_log().create_formatted_copy(evaluation.eval_out_directory / Path("results.csv"),
                                               remove_line_prefixes=["#"])

