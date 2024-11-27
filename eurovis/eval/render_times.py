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

OLD_LOGS = OLD_OVERWRITE
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
cache_voxel =[(["--cache-mode", "v"], "_csh-v", 3)]
cache_brick =[(["--cache-mode", "b"], "_csh-b", 3)]
cache_brick_sm =[(["--cache-mode", "b", "--decode-sm"], "_csh-bsm", 3)]
all_cache = (cache_no, cache_voxel, cache_brick, cache_brick_sm)
# render shading mode
shade_local = [([], "_local", 4)]
shade_shadow = [([], "_shadow", 4)]
shade_ao = [([], "_ao", 4)]
shade_pt = [([], "_pt", 4)]
# other default args
def_volc = [(["--verbose", "--headless", "--cache-size", "4095"], "", 1000)]


def concat_arg_ids(arg_args):
    sorted_by_prio = sorted(arg_args, key=lambda a: a[2])
    return ''.join([a[1] for a in sorted_by_prio])

def csgv_out_name(arg_args):
    sorted_by_prio = sorted(arg_args, key=lambda a: a[2])
    return [(["-c", OUTPUT_BASE_DIR + "/" + concat_arg_ids(arg_args) + ".csgv"], "", 1000)]

def csgv_in_name(arg_args):
    sorted_by_prio = sorted(arg_args, key=lambda a: a[2])
    return [([OUTPUT_BASE_DIR + "/" + concat_arg_ids(arg_args) + ".csgv"], "", 1000)]

def img_name(arg_args):
    sorted_by_prio = sorted(arg_args, key=lambda a: a[2])
    return [(["-i", str(eval_dir / Path(concat_arg_ids(arg_args))) + ".png"], "", 1000)]

def img_name_jpg(arg_args):
    sorted_by_prio = sorted(arg_args, key=lambda a: a[2])
    return [(["-i", str(eval_dir / Path(concat_arg_ids(arg_args))) + ".jpg"], "", 1000)]

def video_name(arg_args):
    sorted_by_prio = sorted(arg_args, key=lambda a: a[2])
    return [(["-v", str(eval_dir / Path(concat_arg_ids(arg_args))) + ".jpg"], "", 1000)]

def vcfg_name(data_arg, shade_arg):
    resolution = "1080x1920" if data_arg[0][1] == "h01" else "1920x1080"
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
    args = ["./volcanite", "--eval-logfile", str(log_path)]
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
            if fallback_log:
                log_manual(fallback_log) # output an empty entry to the log file
            else:
                exit(res.returncode)

def log_manual(output):
    if DRY_RUN:
        print(output)
        return
    with open(str(log_path), "a") as log_out:
        log_out.write(output)

def log_newline():
    if DRY_RUN:
        print("\\\\")
        return
    with open(str(log_path), "a") as log_out:
        log_out.write("\\\\\n")


def create_csv_tex_from_log():
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

if __name__ == "__main__":

    DRY_RUN = False
    if len(sys.argv) > 1 and "dry" in sys.argv[1]:
        DRY_RUN = True
        print("Performing dry run")

    # preliminaries ---------------------------------------------------------------------------------
    script_path = Path(sys.argv[0])
    script_dir = script_path.resolve().absolute().parent
    config_dir = (script_dir / Path("config")).absolute()
    eval = script_path.stem
    eval_dir = Path(OUTPUT_BASE_DIR) / Path(eval)

    tmp_log_path = script_dir / Path(eval + "_tmp.log")
    log_path = eval_dir / Path(eval + ".log")
    tex_path = eval_dir / Path(eval + ".tex")
    csv_path = eval_dir / Path(eval + ".csv")
    out_path = eval_dir / Path(eval + "_output.txt")
    print("Evaluation " + eval + " from template " + str(tmp_log_path) + "\n Directory: " + str(eval_dir)
           + "\n log file: "  + str(log_path) + "\n tex file: " + str(tex_path) + "\n csv file: " + str(csv_path) + "\n out file: " + str(out_path))

    if not DRY_RUN:
        # checkout and build volcanite
        build_volcanite()
        if not (Path(VOLCANITE_BUILD_DIR) / Path("volcanite")).exists():
            print("ERROR: volcanite executable not found at " + str(Path(VOLCANITE_BUILD_DIR) / Path("volcanite")))
            exit(2)

        # set up log files
        eval_dir.mkdir(parents=True, exist_ok=True)
        if OLD_LOGS == OLD_OVERWRITE:
            print("OVERWRITING ANY PRE-EXISTING LOG FILES")
            sleep(5)
            log_path.unlink(missing_ok=True)
            tex_path.unlink(missing_ok=True)
            csv_path.unlink(missing_ok=True)
            out_path.unlink(missing_ok=True)
        if log_path.exists():
            if OLD_LOGS == OLD_ABORT:
                print("ERROR: log file " + str(log_path) + " already exists.")
                exit(1)
        else:
            if not tmp_log_path.exists():
                print("ERROR: template log file " + str(tmp_log_path) + " does not exist.")
                exit(1)
            shutil.copy(tmp_log_path, log_path)
            out_path.unlink(missing_ok=True)

    # evaluation ------------------------------------------------------------------------------------

    log_manual("# " + datetime.now().strftime("%Y-%m-%dT%H:%M:%S.%f") + "\n")

    shade_tex = ["local shading", "shadow rays", "ambient occlusion", "path tracing"]
    cache_mode_tex = ["no cache ", "voxel cache ", "voxel + empty space ", "brick cache ", "brick cache (sm) "]

    for shade_i, shade in enumerate([shade_local, shade_shadow, shade_ao, shade_pt]):
        log_manual("\\midrule\n")
        log_manual("& \\multicolumn{12}{c}{shading mode: " + shade_tex[shade_i] + "} ")
        log_newline()
        for cache_mode_i, cache_mode in enumerate([cache_no, cache_voxel, cache_brick, cache_brick_sm]): 
            log_manual(cache_mode_tex[cache_mode_i] + "\n")
            for enc_mode in [e_rans, e_wmh_nosb, e_wmh]:
                for data in [d_cells, d_fiber, d_h01, d_azba]:
                    bs = bs_64 if data == d_h01 else bs_32
                    if enc_mode == e_rans and cache_mode != cache_brick:
                        log_manual("& - \n")
                    if enc_mode == e_wmh_nosb and data == d_h01:
                        log_manual("& - \n")
                    else:
                        volcanite(def_volc + cache_mode + vcfg_name(data, shade) + rec_name(data)
                                   + img_name_jpg(data + enc_mode + cache_mode + shade)
                                   + csgv_in_name(data + enc_mode + bs),
                                  fallback_log="& - \n",
                                  eval_name=concat_arg_ids(data + enc_mode + cache_mode + shade))
                        if not DRY_RUN:
                            sleep(5)
            log_newline()
            

    # cleanup and log file conversion ---------------------------------------------------------------
    if not DRY_RUN:
        create_csv_tex_from_log()

    exit(0)
