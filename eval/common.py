from volcanite.volcaniteeval import VolcaniteArg

def data_specific_args(data : str) -> list[VolcaniteArg]:
    vargs = []
    if data == "Motta2019":
        vargs.append(VolcaniteArg("--cache-palette"))
        vargs.append(VolcaniteArg("--cache-size 1024"))
    elif data == "Griesser2022-sample":
        vargs.append(VolcaniteArg("--cache-palette"))
        vargs.append(VolcaniteArg("--cache-size 2048"))
    elif data =="H01-wm":
        vargs.append(VolcaniteArg("--stream-lod"))
        vargs.append(VolcaniteArg("--cache-size 4095"))
    else:
        vargs.append(VolcaniteArg("--cache-size 4095"))
    return vargs
