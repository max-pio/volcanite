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


csgv_df = pd.read_csv("../results/compression-eval/compression-eval.csv", comment="#")
csgv_data_set_count = len(csgv_df["Data Set"].unique())
csgv_df["Voxels"] = csgv_df["DimX"] * csgv_df["DimY"] * csgv_df["DimZ"]
csgv_df["Labels/Voxels"] = csgv_df["Labels"] / csgv_df["Voxels"]
csgv_df["Import IO Time [s]"] = csgv_df["Compression Time Total with IO [s]"] - csgv_df["Compression Time Total [s]"]
csgv_df["table_name"] = ""; csgv_df.loc[0, "table_name"] = f"\\multirow{{{csgv_data_set_count}}}{{*}}{{\\rotatebox[origin=c]{{90}}{{Volcanite}}}}"
csgv_df["empty"] = ""

vtk_df = pd.read_csv("../results/vtk-eval/vtk-eval.csv", comment="#")
vtk_data_set_count = len(vtk_df["Data Set"].unique())
vtk_df["empty"] = ""
vtk_df["table_name"] = ""; vtk_df.loc[0, "table_name"] = f"\\multirow{{{vtk_data_set_count}}}{{*}}{{\\rotatebox[origin=c]{{90}}{{VTK}}}}"
vtk_df = vtk_df.merge(csgv_df[["Data Set", "Orig Size [GB]"]], on="Data Set", how="left")

ng_df = pd.read_csv("../results/neuroglancer-eval/neuroglancer-eval.csv", comment="#")
ng_data_set_count = len(ng_df["Data Set"].unique())
ng_df["preprocess IO time [s]"] = ng_df["Precomputed Time [s]"] + ng_df["Meshing Time [s]"]
ng_df["Total Size (gzip) [GB]"] = ng_df["Mesh Size (gzip) [GB]"] + ng_df["Precomputed Size (gzip) [GB]"]
ng_df["Total Size [GB]"] = ng_df["Mesh Size [GB]"] + ng_df["Precomputed Size [GB]"]
ng_df["empty"] = ""
ng_df["table_name"] = ""; ng_df.loc[0, "table_name"] = f"\\multirow{{{ng_data_set_count}}}{{*}}{{\\rotatebox[origin=c]{{90}}{{Neuroglancer}}}}"


times_path = Path("../results/tables/tab-tools-preprocess_times.tex")
times_path.parent.mkdir(parents=True, exist_ok=True)
with open(times_path, 'w') as f:
    f.write("\\begin{tabular}{clrrrrrr}\n")

    # Volcanite
    f.write("% For Volcanite, compression only (without IO) includes freq. prepass and main pass.\n")
    f.write("& Data Set & Compr. only [s] & File IO [s] & Total with IO [s] & TTFF [s] & Size [GB] & (gzip) [GB] \\\\\n")
    f.write("\\midrule\n")
    f.write(df_to_latex_rows(csgv_df[["table_name", "Data Set", "Compression Time Total [s]", "Import IO Time [s]",
                                      "Compression Time Total with IO [s]", "Time To First Frame [s]",
                                      "CSGV Size [GB]", "empty"]],
                             ["{}", "\\dataNameFromCSV{{{}}}", "{:.3f}", "{:.3f}", "{:.3f}", "{:.3f}", "{:.3f}", "{}"]))

    # VTK
    f.write("% For VTK, preprocessing time is the IO file import. TTFF includes GPU uploads etc.\n")
    f.write("& Data Set & & & Total with IO [s] & TTFF [s] & Size [GB] & (gzip) [GB] \\\\\n")
    f.write("\\midrule\n")
    f.write(df_to_latex_rows(vtk_df[["table_name", "Data Set", "empty", "empty",
                                     "preprocess IO time [s]","time to first frame [s]",
                                     "Orig Size [GB]", "empty"]],
                             ["{}", "\\dataNameFromCSV{{{}}}", "{}", "{}", "{:.3f}", "{:.3f}", "{:.3f}", "{}"]))

    # Neuroglancer
    f.write("% For neuroglancer, all timings are with IO included (not separable).\n")
    f.write("& Data Set & Compr. Segm. [s] & Meshing [s] & Total with IO [s] & & Size [GB] & (gzip) [GB] \\\\\n")
    f.write("\\midrule\n")
    f.write(df_to_latex_rows(ng_df[["table_name", "Data Set", "Precomputed Time [s]", "Meshing Time [s]",
                                    "preprocess IO time [s]", "empty",
                                    "Total Size [GB]", "Total Size (gzip) [GB]"]],
                             ["{}", "\\dataNameFromCSV{{{}}}", "{:.3f}", "{:.3f}", "{:.3f}", "{}", "{:.3f}", "{:.3f}"]))

    f.write("\\end{tabular}\n")

