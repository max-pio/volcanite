import subprocess as subp
from pathlib import Path
import shutil
import sys
import re

OVERWRITE_OLD_LOGS = True
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
cache_brick =[(["--cache-mode", "v"], "_csh-b", 3)]
cache_brick_sm =[(["--cache-mode", "v", "--decode-sm"], "_csh-bsm", 3)]
all_cache = (cache_no, cache_voxel, cache_brick, cache_brick_sm)
# other default args
def_volc = [(["--verbose", "--headless", "--cache-size", "3000"], "", 1000)]


def csgv_out_name(arg_args):
    sorted_by_prio = sorted(arg_args, key=lambda a: a[2])
    return [(["-c", OUTPUT_BASE_DIR + "/" + ''.join([a[1] for a in sorted_by_prio]) + ".csgv"],)]

def csgv_in_name(arg_args):
    sorted_by_prio = sorted(arg_args, key=lambda a: a[2])
    return [([OUTPUT_BASE_DIR + "/" + ''.join([a[1] for a in sorted_by_prio]) + ".csgv"],)]

def img_name(arg_args):
    sorted_by_prio = sorted(arg_args, key=lambda a: a[2])
    return [(["-i", str(eval_dir / Path(''.join([a[1] for a in sorted_by_prio]))) + ".png"],)]

def video_name(arg_args):
    sorted_by_prio = sorted(arg_args, key=lambda a: a[2])
    return [([str(eval_dir / Path(''.join([a[1] for a in sorted_by_prio]))) + ".jpg"],)]

def build_volcanite():
    subp.run(["git", "checkout", GIT_CHECKOUT], cwd=VOLCANITE_BUILD_DIR)
    subp.run(["git", "pull"], cwd=VOLCANITE_BUILD_DIR)
    subp.run(["cmake", "--build", ".", "-j", "16", "--target", "volcanite"], cwd=VOLCANITE_BUILD_DIR)

def volcanite(arg_args):
    #   [(["-b", "16"], "_b16"), (["-s", "0"], "_nb")]
    args = ["./volcanite", "--eval-logfile", str(log_path)]
    args = args + [a for args in arg_args for a in args[0]]
    print("RUN VOLCANITE -----------------")
    print(" ".join(args))
    print("-------------------------------")
    res = subp.run(["ls", "-l"], cwd="/home/maxpio/code/volcanite/cmake-build-release/volcanite")
    res = subp.run(args, cwd="/home/maxpio/code/volcanite/cmake-build-release/volcanite")
    if res.returncode != 0:
        print("Error: volcanite returned " + str(res.returncode))
        exit(0)

def log_manual(output):
    with open(str(log_path), "a") as log_out:
        log_out.write(output)

def log_newline():
    with open(str(log_path), "a") as log_out:
        log_out.write("\\\\")


def create_csv_tex_from_log():
  with open(log_path, 'r') as log_in:
    log_data = log_in.read()
    # csv: remove comment lines, delete newlines, replace double \\ with newline, replace \% with %
    csv_data = re.sub(r'^#.*\n', '', log_data, re.MULTILINE)
    csv_data = csv_data.replace('\n', '')
    csv_data = csv_data.replace('\\\\', '\n')
    csv_data = csv_data.replace('\\%', '%')
    with open(csv_path, 'w') as csv_out:
        csv_out.write(csv_data)
    # tex: replace # with % comment flag
    with open(tex_path, 'w') as tex_out:
        tex_out.write(log_data.replace('#', '%'))




if __name__ == "__main__":

    # preliminaries ---------------------------------------------------------------------------------
    script_path = Path(sys.argv[0])
    script_dir = script_path.resolve().absolute().parent
    eval = script_path.stem
    eval_dir = Path(OUTPUT_BASE_DIR) / Path(eval)

    tmp_log_path = script_dir / Path(eval + "_tmp.log")
    log_path = eval_dir / Path(eval + ".log")
    tex_path = eval_dir / Path(eval + ".tex")
    csv_path = eval_dir / Path(eval + ".csv")
    out_path = eval_dir / Path(eval + "_output.txt")
    print("Evaluation " + eval + " from template " + str(tmp_log_path) + "\n Directory: " + str(eval_dir)
           + "\n log file: "  + str(log_path) + "\n tex file: " + str(tex_path) + "\n csv file: " + str(csv_path) + "\n out file: " + str(out_path))

    # checkout and build volcanite
    build_volcanite()
    if not (Path(VOLCANITE_BUILD_DIR) / Path("volcanite")).exists():
        print("ERROR: volcanite executable not found at " + str(Path(VOLCANITE_BUILD_DIR) / Path("volcanite")))
        exit(2)

    # set up log files
    if OVERWRITE_OLD_LOGS:
        log_path.unlink(missing_ok=True)
        tex_path.unlink(missing_ok=True)
        csv_path.unlink(missing_ok=True)
        out_path.unlink(missing_ok=True)
    if log_path.exists():
        print("ERROR: log file " + str(log_path) + " already exists.")
        exit(1)
    if not tmp_log_path.exists():
        print("ERROR: template log file " + str(tmp_log_path) + " does not exist.")
        exit(1)
    eval_dir.mkdir(parents=True, exist_ok=True)
    shutil.copy(tmp_log_path, log_path)
    out_path.unlink(missing_ok=True)


    # evaluation ------------------------------------------------------------------------------------
    volcanite(def_volc + e_nibble + bs_16 + csgv_out_name(d_cells + e_nibble + bs_16) + d_cells)
    log_newline()

    # cleanup and log file conversion ---------------------------------------------------------------
    create_csv_tex_from_log()