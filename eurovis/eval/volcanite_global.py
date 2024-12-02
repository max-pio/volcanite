import subprocess as subp
from pathlib import Path
import shutil
import sys
import re
from datetime import datetime
from time import sleep


OLD_ABORT = 0
OLD_APPEND = 1
OLD_OVERWRITE = 2

# CONFIG, SET IN EVALUATION SCRIPTS:
# NO_LOG = False
# VIDEO_CREATE = False
# OLD_LOGS = OLD_OVERWRITE

GIT_CHECKOUT = "mp/parallel-decode"
OUTPUT_BASE_DIR =  "/home/maxpio/code/volcanite/eurovis/eval/out"
VOLCANITE_BUILD_DIR = "/home/maxpio/code/volcanite/cmake-build-release"

# VOLCANITE ARGUMENT TUPLES
# [(list of args to volcanite call, file name extension of compression output, file name order sort id)]
# data set
d_cells = [(["/home/maxpio/data/ev/cells/cells_frame055.raw"], "cells", 0)]
d_fiber = [(["/home/maxpio/data/ev/fiber/fiberpolymer_1579x1092x1651_16bit.hdf5"], "fiber", 0)]
d_h01 = [(["--chunked", "4,5,5", "/home/maxpio/data/ev/h01/chunks/x{}y{}z{}.hdf5"], "h01", 0)]
d_azba = [(["/home/maxpio/data/ev/azba/AZBA.hdf5"], "azba", 0)]
all_d = (d_cells, d_fiber, d_h01, d_azba)
# encoding mode
e_nibble = [(["-s", "0"], "_nb", 1)]
e_nibble_ra = [(["-s", "0", "-p"], "_nb-ra", 1)]
e_rans = [(["-s", "2"], "_rans", 1)]
e_wmh_nosb = [(["-s", "2", "-p", "-o" ,"pnl"], "_wm", 1)]
e_wmh = [(["-s", "2", "-p", "-o" ,"a"], "_wm-sb", 1)]
all_e = (e_nibble, e_nibble_ra, e_rans, e_wmh_nosb, e_wmh)
# brick size
bs_16 = [(["-b", "16"], "_b16", 2)]
bs_32 = [(["-b", "32"], "_b32", 2)]
bs_64 = [(["-b", "64"], "_b64", 2)]
all_bs = (bs_16, bs_32, bs_64)
# render cache mode
cache_no =[(["--cache-mode", "n"], "_csh-n", 3)]
cache_voxel =[(["--cache-mode", "v", "--empty-space-res", "0"], "_csh-v", 3)]
cache_voxel_es =[(["--cache-mode", "v", "--empty-space-res", "2"], "_csh-v_es", 3)]
cache_brick =[(["--cache-mode", "b"], "_csh-b", 3)]
cache_brick_sm =[(["--cache-mode", "b", "--decode-sm"], "_csh-bsm", 3)]
all_cache = (cache_no, cache_voxel, cache_brick, cache_brick_sm)
# render shading mode
shade_local = [([], "_local", 0.5)]
shade_shadow = [([], "_shadow", 0.5)]
shade_ao = [([], "_ao", 0.5)]
shade_pt = [([], "_pt", 0.5)]
# other default args
def_volc = [(["--verbose", "--headless"], "", 1000)]


def concat_arg_ids(arg_args):
    sorted_by_prio = sorted(arg_args, key=lambda a: a[2])
    return ''.join([a[1] for a in sorted_by_prio])

def csgv_out_name(arg_args):
    return [(["-c", OUTPUT_BASE_DIR + "/" + concat_arg_ids(arg_args) + ".csgv"], "", 1000)]

def csgv_in_name(arg_args):
    return [([OUTPUT_BASE_DIR + "/" + concat_arg_ids(arg_args) + ".csgv"], "", 1000)]

def img_name(arg_args):
    return [(["-i", str(eval_dir / Path(concat_arg_ids(arg_args))) + ".png"], "", 1000)]

def img_name_jpg(arg_args):
    return [(["-i", str(eval_dir / Path(concat_arg_ids(arg_args))) + ".jpg"], "", 1000)]

def video_name(arg_args, create_dir=True):
    video_dir = (eval_dir / Path(concat_arg_ids(arg_args))).absolute()
    if create_dir:
        video_dir.mkdir(parents=True, exist_ok=True)
    return [(["-v", str(video_dir) + "/" + concat_arg_ids(arg_args) + "_{:04}.jpg"], "", 1000)]

def create_mp4(video_arg):
    _tmp = video_arg[0][0][1]
    _dir = Path(_tmp).parent
    _name = Path(_tmp).name

    prefix = _name[:_name.find("{")]
    files = prefix + "*" + _name[_name.rfind("}")+1:]
    cmd = "ffmpeg -n -framerate 60 -pattern_type glob -i '" + files + "' -c:v libx264 -pix_fmt yuv420p " + prefix + ".mp4"
    print("Creating video file in " + str(_dir.absolute()) + " with\n  " + cmd)
    if not DRY_RUN or VIDEO_CREATE:
        subp.run(cmd, cwd=str(_dir.absolute()), shell=True)

def vcfg_name(data_arg, shade_arg):
    resolution = "1080x1920" if data_arg[0][1] == "azba" else "1920x1080"
    return [(["--config", str(config_dir / Path(data_arg[0][1] + shade_arg[0][1] + ".vcfg")), "--resolution", resolution], "", 1000)]

def rec_name(data_arg):
    return [(["--record-in", str(config_dir / Path(data_arg[0][1] + ".rec"))], "", 1000)]

def build_volcanite():
    subp.run(["git", "checkout", GIT_CHECKOUT], cwd=VOLCANITE_BUILD_DIR)
    res = subp.run(["git", "pull"], cwd=VOLCANITE_BUILD_DIR)
    if res.returncode != 0:
        print("Error: git pull returned " + str(res.returncode))
        exit(res.returncode)
    res = subp.run(["cmake", "--build", ".", "-j", "16", "--target", "volcanite"], cwd=VOLCANITE_BUILD_DIR)
    if res.returncode != 0:
        print("Error: building volcanite returned " + str(res.returncode))
        exit(res.returncode)

def volcanite(arg_args, fallback_log=None, eval_name=None):
    if eval_name is None:
        eval_name = concat_arg_ids(arg_args)
    args = ["./volcanite"]
    if not NO_LOG:
        args = args + ["--eval-logfile", str(log_path)]
        if eval_name:
            args = args + ["--eval-name", eval_name]
    # arg_args example:   [(["-b", "16"], "_b16"), (["-s", "0"], "_nb")]
    args = args + [a for args in arg_args for a in args[0]]
    print("RUN VOLCANITE -----------------  " + eval_name)
    print(" ".join(args))
    print("-------------------------------")
    if not DRY_RUN:
        res = subp.run(args, cwd="/home/maxpio/code/volcanite/cmake-build-release/volcanite")
        if res.returncode != 0:
            print("Error: volcanite returned " + str(res.returncode))
            if (not NO_LOG) and fallback_log:
                log_manual(fallback_log.replace("%name", eval_name) + "\n") # output an empty entry to the log file
            else:
                exit(res.returncode)

def log_manual(output):
    if NO_LOG:
        return
    if DRY_RUN:
        print(output)
        return
    with open(str(log_path), "a") as log_out:
        log_out.write(output)

def log_newline():
    if NO_LOG:
        return
    if DRY_RUN:
        print("\\\\")
        return
    with open(str(log_path), "a") as log_out:
        log_out.write("\\\\\n")



def create_csv_tex_from_log():
  if NO_LOG:
      return
  with open(log_path, 'r') as log_in:
    log_data = log_in.read()
    # csv: remove comment lines, delete newlines, replace double \\ with newline,
    #      replace \% with %, replace & with ,
    csv_data = re.sub(r'^#.*\n', '', log_data, re.MULTILINE)
    csv_data = csv_data.replace('\n', '')
    csv_data = csv_data.replace('\\\\', '\n')
    csv_data = csv_data.replace('\\%', '%')
    csv_data = csv_data.replace('&', ',')
    with open(csv_path, 'w') as csv_out:
        csv_out.write(csv_data)
    # tex: replace # with % comment flag
    with open(tex_path, 'w') as tex_out:
        tex_out.write(log_data.replace('#', '%'))
