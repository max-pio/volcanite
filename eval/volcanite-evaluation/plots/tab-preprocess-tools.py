#  Copyright (C) 2026, Max Piochowiak, Karlsruhe Institute of Technology
#
#  This program is free software: you can redistribute it and/or modify
#  it under the terms of the GNU General Public License as published by
#  the Free Software Foundation, either version 3 of the License, or
#  (at your option) any later version.
#
#  This program is distributed in the hope that it will be useful,
#  but WITHOUT ANY WARRANTY; without even the implied warranty of
#  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
#  GNU General Public License for more details.
#
#  You should have received a copy of the GNU General Public License
#  along with this program.  If not, see <https://www.gnu.org/licenses/>.
from pathlib import Path

from timingplots import plot_timings_grouped
from common import *

import itertools
import numpy as np
import pandas as pd
import matplotlib.pyplot as plt
from matplotlib.patches import Patch

print("--------------\nTabulating Neuroglancer\\VTK\\Volcanite Preprocessing")

HUMAN_READABLE = True

def format_duration(total_seconds: float) -> str:
    # Separate integer and fractional part
    int_seconds = int(total_seconds)
    frac = total_seconds - int_seconds  # keep as fraction of a second

    days, rem = divmod(int_seconds, 24 * 60 * 60)
    hours, rem = divmod(rem, 60 * 60)
    minutes, seconds = divmod(rem, 60)

    # Add fractional part back to the seconds
    seconds_with_frac = seconds + frac

    parts = []
    if days:
        parts.append(f"{days}d")
    if hours:
        parts.append(f"{hours}h")
    if minutes:
        parts.append(f"{minutes}m")

    sec_str = f"{seconds_with_frac:04.1f}" # .rstrip("0").rstrip(".")
    if sec_str or not parts:
        parts.append(f"{sec_str}s")

    return " ".join(parts)

def add_human_read_timings(df: pd.DataFrame) -> pd.DataFrame:
    s_columns = [col for col in df.columns if col.endswith('[s]')]
    for col in s_columns:
        new_col = f"{col} [h]"
        df[new_col] = df[col].apply(format_duration)
    return df

csgv_df = pd.read_csv("../results/compression-eval/compression-eval.csv", comment="#")
csgv_evaluated_data = csgv_df["Data Set"].unique()
csgv_data_set_count = len(csgv_evaluated_data)
csgv_df["Voxels"] = csgv_df["DimX"] * csgv_df["DimY"] * csgv_df["DimZ"]
csgv_df["Labels/Voxels"] = csgv_df["Labels"] / csgv_df["Voxels"]
csgv_df["Import IO Time [s]"] = csgv_df["Compression Time Total with IO [s]"] - csgv_df["Compression Time Total [s]"]
csgv_df.sort_values(by="Orig Size [GB]", inplace=True, ignore_index=True)
csgv_df["table_name"] = ""; csgv_df.loc[0, "table_name"] = f"\\multirow{{{csgv_data_set_count}}}{{*}}{{\\rotatebox[origin=c]{{90}}{{Volcanite}}}}"
csgv_df["empty"] = ""
csgv_df = add_human_read_timings(csgv_df)

vtk_df = pd.read_csv("../results/vtk-eval/vtk-eval.csv", comment="#")
vtk_evaluated_data = vtk_df["Data Set"].unique()
vtk_data_set_count = len(vtk_evaluated_data)
vtk_df["empty"] = ""
vtk_df = vtk_df.merge(csgv_df[["Data Set", "Orig Size [GB]"]], on="Data Set", how="left")
vtk_df.sort_values(by="Orig Size [GB]", inplace=True, ignore_index=True)
vtk_df["table_name"] = ""; vtk_df.loc[0, "table_name"] = f"\\multirow{{{vtk_data_set_count}}}{{*}}{{\\rotatebox[origin=c]{{90}}{{VTK}}}}"
vtk_df = add_human_read_timings(vtk_df)

ng_df = pd.read_csv("../results/neuroglancer-eval/neuroglancer-eval.csv", comment="#")
ng_evaluated_data = ng_df["Data Set"].unique()
ng_data_set_count = len(ng_evaluated_data)
ng_df["preprocess IO time [s]"] = ng_df["Precomputed Time [s]"] + ng_df["Meshing Time [s]"]
ng_df["Total Size (gzip) [GB]"] = ng_df["Mesh Size (gzip) [GB]"] + ng_df["Precomputed Size (gzip) [GB]"]
ng_df["Total Size [GB]"] = ng_df["Mesh Size [GB]"] + ng_df["Precomputed Size [GB]"]
ng_df["empty"] = ""
ng_df = ng_df.merge(csgv_df[["Data Set", "Orig Size [GB]"]], on="Data Set", how="left")
ng_df.sort_values(by="Orig Size [GB]", inplace=True, ignore_index=True)
ng_df["table_name"] = ""; ng_df.loc[0, "table_name"] = f"\\multirow{{{ng_data_set_count}}}{{*}}{{\\rotatebox[origin=c]{{90}}{{Neuroglancer}}}}"
ng_df = add_human_read_timings(ng_df)

file_df = pd.read_csv("../results/filesize-eval/filesize-eval.csv", comment="#")
vtk_df = vtk_df.merge(file_df[["Data Set", "hdf5 (gzip) Filesize [GB]"]], on="Data Set", how="left")
csgv_df = csgv_df.merge(file_df[["Data Set", "CSGV Filesize [GB]", "CSGV (gzip) Filesize [GB]", "CSGV (lzma) Filesize [GB]"]], on="Data Set", how="left")


times_path = Path("../results/tables/tab-tools-preprocess_times.tex")
times_path.parent.mkdir(parents=True, exist_ok=True)
with open(times_path, 'w') as f:
    f.write("\\begin{tabular}{cl|rrrr|rr}\n")

    if HUMAN_READABLE:
        # Volcanite
        f.write("% For Volcanite, compression only (without IO) includes freq. prepass and main pass.\n")
        f.write(r"& & \multicolumn{4}{l|}{Preprocessing Times [s]} & \multicolumn{2}{l}{File Sizes [GB]} \\" + "\n")
        f.write("& Data Set & Compr. only& File IO& Total with IO& TTFF& Direct & gzip \\\\\n")
        f.write("\\midrule\n")
        f.write(df_to_latex_rows(
            csgv_df[["table_name", "Data Set", "Compression Time Total [s] [h]", "Import IO Time [s] [h]",
                     "Compression Time Total with IO [s] [h]", "Time To First Frame [s] [h]",
                     "CSGV Filesize [GB]", "CSGV (gzip) Filesize [GB]"]],
            ["{}", "\\dataNameFromCSV{{{}}}", "{}", "{}", "{}", "{}", "{:.3f}", "{:.3f}"]))

        # VTK
        f.write(r"\multicolumn{8}{c}{}\\" + "\n")
        f.write("% For VTK, preprocessing time is the IO file import. TTFF includes GPU uploads etc.\n")
        f.write(r"& & \multicolumn{4}{l|}{Preprocessing Times [s]} & \multicolumn{2}{l}{File Sizes [GB]} \\" + "\n")
        f.write("& Data Set & & & Total with IO & TTFF & Direct & gzip \\\\\n")
        f.write("\\midrule\n")
        f.write(df_to_latex_rows(vtk_df[["table_name", "Data Set", "empty", "empty",
                                         "preprocess IO time [s] [h]", "time to first frame [s] [h]",
                                         "Orig Size [GB]", "hdf5 (gzip) Filesize [GB]"]],
                                 ["{}", "\\dataNameFromCSV{{{}}}", "{}", "{}", "{}", "{}", "{:.3f}", "{:.3f}"]))
        missing_vtk = [x for x in csgv_evaluated_data if x not in vtk_evaluated_data]
        if len(missing_vtk) > 0:
            f.write(r"& \dots & \multicolumn{4}{r|}{{\footnotesize "
                    + ", ".join([f"\\dataNameFromCSV{{{d}}}" for d in missing_vtk])
                    + r": out of memory}} & & \\")

        # Neuroglancer
        f.write(r"\multicolumn{8}{c}{}\\" + "\n")
        f.write("% For neuroglancer, all timings are with IO included (not separable).\n")
        f.write(r"& & \multicolumn{4}{l|}{Preprocessing Times [s]} & \multicolumn{2}{l}{File Sizes [GB]} \\" + "\n")
        f.write("& Data Set & Compr. Segm. & Meshing & Total with IO & & Direct & gzip \\\\\n")
        f.write("\\midrule\n")
        f.write(df_to_latex_rows(ng_df[["table_name", "Data Set", "Precomputed Time [s] [h]", "Meshing Time [s] [h]",
                                        "preprocess IO time [s] [h]", "empty",
                                        "Total Size [GB]", "Total Size (gzip) [GB]"]],
                                 ["{}", "\\dataNameFromCSV{{{}}}", "{}", "{}", "{}", "{}", "{:.3f}", "{:.3f}"]))

        missing_ng = [x for x in csgv_evaluated_data if x not in ng_evaluated_data]
        if len(missing_ng) > 0:
            f.write(r"& \dots & \multicolumn{4}{r|}{{\footnotesize "
                    + ", ".join([f"\\dataNameFromCSV{{{d}}}" for d in missing_ng])
                    + r": out of memory}} & & \\")
    else:
        # Volcanite
        f.write("% For Volcanite, compression only (without IO) includes freq. prepass and main pass.\n")
        f.write(r"& & \multicolumn{4}{l|}{Preprocessing Times [s]} & \multicolumn{2}{l}{File Sizes [GB]} \\" + "\n")
        f.write("& Data Set & Compr. only& File IO& Total with IO& TTFF& Direct & gzip \\\\\n")
        f.write("\\midrule\n")
        f.write(df_to_latex_rows(csgv_df[["table_name", "Data Set", "Compression Time Total [s]", "Import IO Time [s]",
                                          "Compression Time Total with IO [s]", "Time To First Frame [s]",
                                          "CSGV Filesize [GB]", "CSGV (gzip) Filesize [GB]"]],
                                 ["{}", "\\dataNameFromCSV{{{}}}", "{:.3f}", "{:.3f}", "{:.3f}", "{:.3f}", "{:.3f}", "{:.3f}"]))

        # VTK
        f.write(r"\multicolumn{8}{c}{}\\" + "\n")
        f.write("% For VTK, preprocessing time is the IO file import. TTFF includes GPU uploads etc.\n")
        f.write(r"& & \multicolumn{4}{l|}{Preprocessing Times [s]} & \multicolumn{2}{l}{File Sizes [GB]} \\" + "\n")
        f.write("& Data Set & & & Total with IO & TTFF & Direct & gzip \\\\\n")
        f.write("\\midrule\n")
        f.write(df_to_latex_rows(vtk_df[["table_name", "Data Set", "empty", "empty",
                                         "preprocess IO time [s]","time to first frame [s]",
                                         "Orig Size [GB]", "hdf5 (gzip) Filesize [GB]"]],
                                 ["{}", "\\dataNameFromCSV{{{}}}", "{}", "{}", "{:.3f}", "{:.3f}", "{:.3f}", "{:.3f}"]))
        missing_vtk = [x for x in csgv_evaluated_data if x not in vtk_evaluated_data]
        if len(missing_vtk) > 0:
            f.write(r"& \dots & \multicolumn{4}{r|}{{\footnotesize "
                    + ", ".join([f"\\dataNameFromCSV{{{d}}}" for d in missing_vtk])
                    + r": out of memory}} & & \\")

        # Neuroglancer
        f.write(r"\multicolumn{8}{c}{}\\" + "\n")
        f.write("% For neuroglancer, all timings are with IO included (not separable).\n")
        f.write(r"& & \multicolumn{4}{l|}{Preprocessing Times [s]} & \multicolumn{2}{l}{File Sizes [GB]} \\" + "\n")
        f.write("& Data Set & Compr. Segm. & Meshing & Total with IO & & Direct & gzip \\\\\n")
        f.write("\\midrule\n")
        f.write(df_to_latex_rows(ng_df[["table_name", "Data Set", "Precomputed Time [s]", "Meshing Time [s]",
                                        "preprocess IO time [s]", "empty",
                                        "Total Size [GB]", "Total Size (gzip) [GB]"]],
                                 ["{}", "\\dataNameFromCSV{{{}}}", "{:.3f}", "{:.3f}", "{:.3f}", "{}", "{:.3f}", "{:.3f}"]))

        missing_ng = [x for x in csgv_evaluated_data if x not in ng_evaluated_data]
        if len(missing_ng) > 0:
            f.write(r"& \dots & \multicolumn{4}{r|}{{\footnotesize "
                    + ", ".join([f"\\dataNameFromCSV{{{d}}}" for d in missing_ng])
                    + r": out of memory}} & & \\")

    f.write("\\end{tabular}\n")

