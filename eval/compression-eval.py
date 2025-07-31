from datetime import datetime
from pathlib import Path
from volcanite.volcaniteeval import VolcaniteArg, VolcaniteEvaluation, VolcaniteExec, VolcaniteLogFileCfg, ExistingPolicy

from common import data_specific_compression_args, data_specific_args

if __name__ == "__main__":

    # set up the evaluation output directory and the log files
    evaluation_name = Path(__file__).stem
    with VolcaniteEvaluation(eval_out_directory=f"./results/{evaluation_name}/", existing_policy=ExistingPolicy.DELETE,
                                        eval_name=evaluation_name,
                                        log_files=[VolcaniteLogFileCfg(f"{evaluation_name}.csv",
                                                              fmts=["{volume_dim_x},{volume_dim_y},{volume_dim_z},{volume_labels},{orig_gb},{orig_bits_per_voxel},"
                                                                    "{csgv_gb},{csgv_bits_per_voxel},{comprate_pcnt},{csgv_detail_gb},{csgv_detail_pcnt},{brick_size},"
                                                                    "{brick_palette_size_min},{brick_palette_size_avg},{brick_palette_size_max},"
                                                                    "{brick_palette_duplicates_min},{brick_palette_duplicates_avg},{brick_palette_duplicates_max},"
                                                                    "{brick_labels_min},{brick_labels_avg},{brick_labels_max},"
                                                                    "{mem_cache_mb},{mem_cache_used_mb},{mem_cache_fillrate_pcnt},{mem_cache_packing_factor}"],
                                                              headers=["Data Set,Unlimited Pdelta,DimX,DimY,DimZ,Labels,Orig Size [GB],Orig bits/voxel,"
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

            # compress twice, once with the old and once with the new palette delta
            for arg_unlim_pdelta in [False, True]:

                # the first two columns are written from the python script
                evaluation.get_log().log_manual(arg_csgv.identifier + "," + ("y" if arg_unlim_pdelta else "n"), end="")

                # the raw input data and compression parameters (except operation list) for the compression
                # input data sets are assumed to be structured in subdirectories in the same location as the csgv files, 
                # as created by the download_evaluation_data.py download script.
                arg_data_input = data_specific_compression_args(arg_csgv.identifier, volume_data_dir=VolcaniteArg.get_csgv_directory(), operations=False)

                arg_rendering = data_specific_args(arg_csgv.identifier, cache_palette=False, stream_lod=False)
        
                # execute Volcanite and pass the Volcanite log file into which the results are appended
                # stream-lod is necessary to force detail separation (and obtain the detail encoding size)
                # cache-palette is enabled to obtain cache packing factors
                volcanite.exec([arg_data_input, arg_rendering, VolcaniteArg("--stream-lod"), VolcaniteArg("--cache-palette"), VolcaniteArg("--verbose")])

