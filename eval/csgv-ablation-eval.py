from datetime import datetime
from pathlib import Path
import pandas as pd
from volcanite.volcaniteeval import VolcaniteArg, VolcaniteEvaluation, VolcaniteExec, VolcaniteLogFileCfg, ExistingPolicy

from common import data_specific_compression_args, data_specific_rendering_args

if __name__ == "__main__":

    # set up the evaluation output directory and the log files
    evaluation_name = Path(__file__).stem
    with VolcaniteEvaluation(eval_out_directory=f"./results/{evaluation_name}/", existing_policy=ExistingPolicy.APPEND,
                                        eval_name=evaluation_name,
                                        log_files=[VolcaniteLogFileCfg(f"{evaluation_name}.csv",
                                                              fmts=["{operation_mask},{volume_labels},{orig_gb},{orig_bits_per_voxel},"
                                                                    "{csgv_gb},{csgv_bits_per_voxel},{comprate_pcnt},{csgv_detail_gb},{csgv_detail_pcnt},{brick_size},"
                                                                    "{brick_palette_size_min},{brick_palette_size_avg},{brick_palette_size_max},"
                                                                    "{brick_palette_duplicates_min},{brick_palette_duplicates_avg},{brick_palette_duplicates_max},"
                                                                    "{brick_labels_min},{brick_labels_avg},{brick_labels_max},"
                                                                    "{mem_cache_mb},{mem_cache_used_mb},{mem_cache_fillrate_pcnt},{mem_cache_packing_factor}"],
                                                              headers=["Data Set,Operations,Labels,Orig Size [GB],Orig bits/voxel,"
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
        for stopbit_code in ["", "s"]:

            if stopbit_code == "s":
                evaluation.get_log().log_manual("With Stop Bits,,,,,,,,,,,,,,,,,,,,,,,,")
            else:
                evaluation.get_log().log_manual("Without Stop Bits,,,,,,,,,,,,,,,,,,,,,,,,")
            
            for arg_csgv in VolcaniteArg.args_csgv_datasets.values():

                if not arg_csgv.identifier in ["pa66","Griesser2022-sample","Ara2016","cells","xtm-battery","azba", "H01-bloodvessel", "Wolny2020", "liconn", "fiber", "Motta2019-small", "Griesser2022-validation"]:
                    continue

                # H01-wm is too large to be processed on our evaluation system with 64 GB of RAM if not compressed with all operations
                if arg_csgv.identifier in ["H01-wm", "Motta2019"]:
                    continue

                # compress three times: without any delta, with the old (1 < delta < 17) and once with the new (unlimited) palette delta
                for operation_codes in ["p", "px", "pxy", "pxyz", "pxyzl", "pxyzld-", "pxyzld"]:

                    # the first column is written from the python script
                    evaluation.get_log().log_manual(arg_csgv.identifier + ",", end="")

                    # the raw input data and compression parameters (except operation list) for the compression
                    # input data sets are assumed to be structured in subdirectories in the same location as the csgv files, 
                    # as created by the download_evaluation_data.py download script.
                    args_data_input = data_specific_compression_args(arg_csgv.identifier, volume_data_dir=VolcaniteArg.get_csgv_directory(), operations=False, brick_size=False)

                    # evaluate prefix-combinations of stop bits
                    arg_operation = VolcaniteArg.arg_operations(operation_codes + stopbit_code)

                    args_rendering = data_specific_rendering_args(arg_csgv.identifier, cache_palette=False, stream_lod=False)

                    # chunked data must have a decompression path
                    csgv_out_path = Path(f"./results/{evaluation_name}/{VolcaniteArg.concat_ids([arg_csgv, arg_operation])}.csgv")
                    arg_csgv_export = VolcaniteArg(["-c", str(csgv_out_path.resolve())])

                    # execute Volcanite and pass the Volcanite log file into which the results are appended
                    # stream-lod is necessary to force detail separation (and obtain the detail encoding size)
                    # cache-palette is enabled to obtain cache packing factors
                    volcanite.exec(args_data_input + args_rendering + [arg_operation, VolcaniteArg.args_brick_size["64"], VolcaniteArg("--verbose"),
                                                                       arg_csgv_export, VolcaniteArg("--stream-lod"), VolcaniteArg("--cache-palette")])

                    # remove the csgv file, otherwise this would store hundreds of GB
                    csgv_out_path.resolve().unlink()

    # separate ablation evaluation results into separate files for each data set
    df = pd.read_csv(f'./results/{evaluation_name}/{evaluation_name}.csv',  comment='#')
    df["Stop Bit"] = df["Operations"].str.endswith("s").map({True: 'y', False: 'n'})
    df["Operations"] = df["Operations"].str.rstrip("s")
    # export Operations and compresion rate to each unique data set's csv file
    for data_name in df["Data Set"].unique():
        filtered = df[df["Data Set"] == data_name][["Operations", "Stop Bit", "Compression Rate [Pcnt]"]]
        filtered.to_csv(f"./results/{evaluation_name}/{evaluation_name}_{data_name}.csv", index=False)
