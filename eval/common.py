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
