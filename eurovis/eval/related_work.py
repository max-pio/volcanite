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
d_h01_eval_chunk = [(["/home/maxpio/data/ev/h01/chunks/x0y5z3.hdf5"], "h01-053", 0)]
d_azba = [(["/home/maxpio/data/ev/azba/AZBA.hdf5"], "azba", 0)]
all_d = (d_cells, d_fiber, d_h01, d_azba)



def build_eval_related_work():
    subp.run(["git", "checkout", GIT_CHECKOUT], cwd=VOLCANITE_BUILD_DIR)
    res = subp.run(["git", "pull"], cwd=VOLCANITE_BUILD_DIR)
    if res.returncode != 0:
        print("Error: git pull returned " + str(res.returncode))
        exit(res.returncode)
    res = subp.run(["cmake", "--build", ".", "-j", "16", "--target", "eval_related_work"], cwd=VOLCANITE_BUILD_DIR)
    if res.returncode != 0:
        print("Error: building volcanite returned " + str(res.returncode))
        exit(res.returncode)

def eval_related_work(arg_args, fallback_log=None, eval_name=None):
    if eval_name is None:
        eval_name = concat_arg_ids(arg_args)
    args = ["./eval_related_work"]

    # arg_args example:   [(["-b", "16"], "_b16"), (["-s", "0"], "_nb")]
    args = args + [a for args in arg_args for a in args[0]]
    print("RUN EVAL_RELATED_WORK ---------  " + eval_name)
    print(" ".join(args))
    print("-------------------------------")
    if not DRY_RUN:
        res = subp.run(args, cwd="/home/maxpio/code/volcanite/cmake-build-release/eval_related_work")
        if res.returncode != 0:
            print("Error: eval_related_work returned " + str(res.returncode))
            exit(res.returncode)



if __name__ == "__main__":
    # preliminaries ---------------------------------------------------------------------------------
    script_path = Path(sys.argv[0])
    script_dir = script_path.resolve().absolute().parent
    config_dir = (script_dir / Path("config")).absolute()
    eval = script_path.stem
    eval_dir = Path(OUTPUT_BASE_DIR) / Path(eval)

    out_path = eval_dir / Path(eval + "_output.txt")

    DRY_RUN = False
    if len(sys.argv) > 1 and any(["dry" in a for a in sys.argv[1:]]):
        DRY_RUN = True
        print("Performing dry run")


    print("Evaluation " + eval + "\n Directory: " + str(eval_dir) + "\n out file: " + str(out_path))

    if not DRY_RUN:
        # checkout and build volcanite
        build_eval_related_work()
        if not (Path(VOLCANITE_BUILD_DIR) / Path("eval_related_work")).exists():
            print("ERROR: eval_related_work executable not found at " + str(Path(VOLCANITE_BUILD_DIR) / Path("eval_related_work")))
            exit(2)

        # set up log files
        eval_dir.mkdir(parents=True, exist_ok=True)
        if OLD_LOGS == OLD_OVERWRITE:
            print("OVERWRITING ANY PRE-EXISTING LOG FILES")
            sleep(5)
            out_path.unlink(missing_ok=True)
        if out_path.exists():
            if OLD_LOGS == OLD_ABORT:
                print("ERROR: log file " + str(log_path) + " already exists.")
                exit(1)

    # evaluation ------------------------------------------------------------------------------------
    print("# " + datetime.now().strftime("%Y-%m-%dT%H:%M:%S.%f") + "\n")

    for data in [d_cells, d_fiber, d_h01_eval_chunk, d_azba]:
        eval_related_work(data)
