from volcanite.volcaniteeval import VolcaniteArg
from pathlib import Path

def data_specific_args(data : str, cache_palette : bool = True, stream_lod : bool = True, cache_size : bool = True) -> list[VolcaniteArg]:
    vargs = []

    if cache_palette:
        if data == "Motta2019" or data == "Griesser2022-sample" or data == "H01-wm"\
            or data == "pa66" or data == "fiber" or data == "H01-bloodvessel":
            vargs.append(VolcaniteArg("--cache-palette"))

    if stream_lod:
        if data == "H01-wm":
            vargs.append(VolcaniteArg("--stream-lod"))

    if cache_size:
        if data == "Motta2019":
            vargs.append(VolcaniteArg("--cache-size 1024"))
        elif data == "Griesser2022-sample":
            vargs.append(VolcaniteArg("--cache-size 2048"))
        else:
            vargs.append(VolcaniteArg("--cache-size 4095"))

    return vargs


def data_specific_compression_args(data: str, volume_data_dir: Path, input_file: bool = True, brick_size: bool = True, operations: bool = True):
    vargs = []
    
    if volume_data_dir:
        _chunked = None
        _input_path = None
        if data == "azba":
            _input_path = volume_data_dir / data / "azba.hdf5"
        elif data == "Ara2016":
            _chunked = (2,1,2)
            _input_path = volume_data_dir / data / "Ara2016_x{}y{}z{}.hdf5"
        elif data == "pa66":
            _input_path = volume_data_dir / data / "pa66_segm.hdf5"
        elif data == "Wolny2020":
            _input_path = volume_data_dir / data / "Wolny2020.hdf5"
        elif data == "Griesser2022-validation":
            _chunked = (1,1,0)
            _input_path = volume_data_dir / data / "Griesser2022-validation_x{}y{}z{}.hdf5"
        elif data == "xtm-battery":
            _input_path = volume_data_dir / data / "xtm-battery.hdf5"
        elif data == "Motta2019-small":
            _input_path = volume_data_dir / data / "Motta2019_x2y3z2.hdf5"
        elif data == "cells":
            _input_path = volume_data_dir / data / "cells_065.hdf5"
        elif data == "fiber":
            _input_path = volume_data_dir / data / "maurer_glassfiberpolymer.hdf5"
        elif data == "Motta2019":
            _chunked = (5,8,3)
            _input_path = volume_data_dir / data / "x{}y{}z{}.hdf5"
        elif data == "H01-wm":
            _chunked = (9,9,5)
            _input_path = volume_data_dir / data / "H01-wm_x{}y{}z{}.hdf5"
        elif data == "H01-bloodvessel":
            _chunked = (9,9,0)
            _input_path = volume_data_dir / data / "H01-bloodvessel_x{}y{}z{}.hdf5"
        elif data == "liconn":
            _chunked = (3,4,0)
            _input_path = volume_data_dir / data / "liconn_x{}y{}z{}.hdf5"
        elif data == "Griesser2022-sample":
            _chunked = (9,3,2)
            _input_path = volume_data_dir / data / "Griesser2022-sample_x{}y{}z{}.hdf5"

        if not _input_path:
            raise ValueError(f"No input volume path found for {data}.")
        
        if _chunked:
            vargs.append(VolcaniteArg(["--chunked", f"{_chunked[0]},{_chunked[1]},{_chunked[2]}", str(_input_path)]))
        else:
            vargs.append(VolcaniteArg([str(_input_path)]))

    if brick_size:
        if data in ["motta2019","griesser2022-sample","h01-wm","h01-bloodvessel"]:
            vargs.append(VolcaniteArg.args_brick_size["64"])
        else:
            vargs.append(VolcaniteArg.args_brick_size["32"])

    if operations:
        # unlimited Palette delta (optimal for cache paletting)
        if data == "Motta2019" or data == "Griesser2022-sample" or data == "pa66" or data == "fiber":
            vargs.append(VolcaniteArg("-o pnlds"))
        # old Palette delta (better compression rates)
        elif data in ["h01-wm", "h01-bloodvessel"]:
            vargs.append(VolcaniteArg("-o pnld-s"))
        # no Palette delta (faster rendering)
        else:
            vargs.append(VolcaniteArg("-o pnls"))

    return vargs


def is_bigdataataset(data : str) -> bool:
    """
    :returns: True if this is a large data set (Griesser2022-Sample, H01-WM, Motta2019).
    Use this to skip large data sets in evaluation scripts.
    """

    return data.lower() in ["griesser2022-sample","h01-wm","motta2019"]

