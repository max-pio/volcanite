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

OLD_LOGS = OLD_APPEND
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


def import_latex_table(tex_path):
    entry_for_eval = {}
    with open(tex_path, 'r') as file:
        data = file.read()
        entries = re.split('\n', data)
    for e in entries:
        if "%" in e:
            parts = e.split('%')
            value = parts[0]
            name = parts[1].strip().split()[0]
            entry_for_eval[name] = value
            print(name + ": " + entry_for_eval[name])

    return entry_for_eval


if __name__ == "__main__":

    import_path = "/home/maxpio/code/volcanite/eurovis/eval/out/render_times/render_times_highlight.tex"
    output_path = import_path[:import_path.rfind(".")] + "_transposed.tex"
    print(import_path + "\ntransposing to\n" + output_path)
    results = import_latex_table(import_path)

    with open(output_path, 'w') as tex_out:

        shade_tex = ["local shading", "shadow rays", "ambient occlusion", "path tracing"]
        enc_tex = ["\\gls{csgv}", "\\gls{csgvr}", "\\gls{csgvr}+sb"]
        cache_mode_tex = ["no cache ", "voxel cache ", "voxel cache (es)", "brick cache ", "brick cache (sm) "]

        for enc_mode_i, enc_mode in enumerate([e_rans, e_wmh_nosb, e_wmh]):
            tex_out.write("\\midrule\n")

            if enc_mode == e_rans:
                tex_out.write("\\multirow{1}{*}{" + enc_tex[enc_mode_i] + "}\n")
            else:
                tex_out.write("\\multirow{5}{*}{" + enc_tex[enc_mode_i] + "}\n")

            for cache_mode_i, cache_mode in enumerate([cache_no, cache_voxel, cache_voxel_es, cache_brick, cache_brick_sm]):

                if enc_mode == e_rans and cache_mode != cache_brick:
                    continue

                tex_out.write("& " + cache_mode_tex[cache_mode_i] + "\n")

                for shade_i, shade in enumerate([shade_local, shade_shadow, shade_ao]): # without shade_pt
                    for data in [d_cells, d_fiber, d_h01, d_azba]:
                        bs = bs_64 if data == d_h01 else bs_32
                        # rANS with h01 uses a large cache of 4 GiB, everything else uses 1 GiB
                        cache_size = [(["--cache-size", "1024" if (data != d_h01 or enc_mode != e_rans) else "4095"], "", 1000)]

                        name = concat_arg_ids(data + enc_mode + cache_mode + shade)

                        if name in results:
                            tex_out.write(results[name] + "% " + name + "\n")
                        else:
                            tex_out.write("& -  % " +  name + "\n")

                tex_out.write("\\\\\n")
                
    exit(0)
