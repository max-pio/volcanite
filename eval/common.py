from volcanite.volcaniteeval import VolcaniteArg

def data_specific_args(data : str) -> list[VolcaniteArg]:
    vargs = []
    if data == "Motta2019" or data == "Griesser2022-sample" or data == "H01-wm"\
        or data == "pa66" or data == "fiber" or data == "H01-bloodvessel":
        vargs.append(VolcaniteArg("--cache-palette"))

    if data == "H01-wm":
        vargs.append(VolcaniteArg("--stream-lod"))

    if data == "Motta2019":
        vargs.append(VolcaniteArg("--cache-size 1024"))
    elif data == "Griesser2022-sample":
        vargs.append(VolcaniteArg("--cache-size 2048"))
    else:
        vargs.append(VolcaniteArg("--cache-size 4095"))

    return vargs


def is_big_dataset(data : str) -> bool:
    """
    :returns: True if this is a large data set (Griesser2022-Sample, H01-WM, Motta2019).
    Use this to skip large data sets in evaluation scripts.
    """

    return data.lower() in ["griesser2022-sample","h01-wm","motta2019"]

