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

import matplotlib.pyplot as plt
from matplotlib.patches import Patch

from timingplots import plot_timings_grouped
from common import *

import numpy as np
import pandas as pd

print("--------------\nPlotting CSGV Ablation Study")
init_plots()

# load data
ablation_df = pd.read_csv("../results/csgv-ablation-eval/csgv-ablation-eval.csv", comment="#")
data_df = pd.read_csv("../results/csgv-eval/csgv-eval.csv", comment="#")


data_df = data_df[data_df['Data Set'].isin(ablation_df['Data Set'])]
data_sets = data_df["Data Set"]

# evaluated operation combinations
op_runs = ["p", "px", "pxy", "pxyz", "pxyzl", "pxyzld-", "pxyzld"]

# split operation code in data frame into separate column for stop bit and the operations without stop bit
ablation_df["Stop Bit"] = ablation_df["Operations"].str.endswith('s')
ablation_df["Operations"] = ablation_df["Operations"].str.rstrip('s')

# create bar plots
for data in data_sets:

    ys = [ablation_df[ablation_df["Data Set"].eq(data) & (ablation_df["Stop Bit"] == False)].set_index("Operations").reindex(op_runs).reset_index()["Compression Rate [Pcnt]"],
          ablation_df[ablation_df["Data Set"].eq(data) & (ablation_df["Stop Bit"] == True)].set_index("Operations").reindex(op_runs).reset_index()["Compression Rate [Pcnt]"],]

    fig, ax = plot_timings_grouped(np.arange(len(op_runs)), ys,
                                ylabel=r"Compression Rate [\%]",
                                xticklabels=[r"$\cdot$", r"$\cdot$", r"$\cdot$", r"$\cdot$", r"$\cdot$", r"$\cdot$", r"$\cdot$"],
                                labels=["Without Stop Bits", "With Stop Bits"],
                                marknan=False, barwidth=0.5, baroffsetscale=0.4)

    ax.set_title(get_data_set_tex(data))
    ax.legend().set_loc('upper right')

    fig.canvas.draw()

    # add the operation images to the x ticks
    offset_from_tick = (-1, -14)
    offset_from_y = ax.get_ylim()[0] * 0.95
    #
    import matplotlib.image as mpimg
    from matplotlib.offsetbox import OffsetImage, AnnotationBbox
    operations = ["parent", "neighbor_x", "neighbor_y", "neighbor_z", "palette_last", "palette_delta_old", "palette_delta"]
    xticks = ax.get_xticks()
    for i, op in enumerate(operations):
        image = mpimg.imread(f"./img/op_{op}.png")
        imagebox = OffsetImage(image, zoom=0.15, cmap='gray')
        ax.add_artist(AnnotationBbox(imagebox, (xticks[i], offset_from_y), xybox=offset_from_tick, xycoords='data',
                                     boxcoords="offset points",frameon=False, pad=0.0, arrowprops=None, zorder=10))


    save_plot(f"../results/plts/csgv-ablation_{data}.pdf", fig)
    plt.close(fig)
