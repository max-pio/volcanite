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
import numpy as np

from timingplots import plot_timings, plot_timings_grouped
from common import *

import pandas as pd
from pathlib import Path
import matplotlib.pyplot as plt

# CSGV Compression Rates
print("--------------\nPlotting CSGV Bits/Voxel")
init_plots()


# load data
csgv_csv = Path("../results/csgv-eval/csgv-eval.csv")
data_df = pd.read_csv(csgv_csv, comment="#")
data_df["Voxels"] = data_df["DimX"] * data_df["DimY"] * data_df["DimZ"]
data_df["Labels/Voxels"] = data_df["Labels"] / data_df["Voxels"]


for X_AXIS, xlabel, pdfname  in zip(["Labels", "Brick Labels avg"],
                                    [r"Total Label Count", r"Avg. Labels / Brick"],
                                    ["total", "brick"]):

    print(f"  Plotting Bits/Voxel for {X_AXIS}")

    fig, ax = plt.subplots(figsize=(5, 4))
    for i, orig_b in enumerate([8, 16, 32]):
        ax.scatter(data_df[data_df["Orig bits/voxel"] == orig_b][X_AXIS],
                   data_df[data_df["Orig bits/voxel"] == orig_b]["CSGV bits/voxel"],
                   color=volcanite_colors[i], edgecolors=volcanite_colors_dark[i], s=50)
    ax.legend(labels=["Orig  8b", "Orig 16b", "Orig 32B"])
    ax.set_xscale("log")
    if pdfname == "total":
        ax.set_xticks(np.logspace(0, 6, 4))
    else:
        ax.set_xticks(np.logspace(0, 2, 3))

    ax.set_yticks(np.arange(0, 1.1, 0.25))
    ax.grid(axis='y', color='gray', alpha=0.5, linewidth=0.5, zorder=0)
    ax.set_xlabel(xlabel)
    ax.set_ylabel("CSGV Bits / Voxel")

    for x, y, label in zip(data_df[X_AXIS], data_df["CSGV bits/voxel"], data_df["Data Set"]):
        ax.text(x, y, get_data_set_tex(label), fontsize=8, color='gray', rotation=45, ha='left', va='bottom',)

    save_plot(f"../results/plts/bits-per-voxel_{pdfname}.pdf", fig)
    plt.close(fig)

