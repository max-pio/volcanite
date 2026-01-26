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

from timingplots import plot_timings_grouped
from common import *

import itertools
import numpy as np
import pandas as pd
import matplotlib.pyplot as plt

print("--------------\nPlotting VTK Timings")
init_plots()


for SECOND_VALUE_KEY in ["Voxels", "Labels"]:     # sort key for data set order, plotted as red bars behind data
    for SECOND_VALUE_LOG in [False, True]:        # enable logarithmic Y axis for second value key
        for ONLY_VTK_DATA in [False, True]:       # if true, data sets that cannot be rendered with VTK are not plotted

            print(f"  {SECOND_VALUE_KEY.lower()}{" (log)" if SECOND_VALUE_LOG else ""}{"" if ONLY_VTK_DATA else " all data"}")

            # load data
            vtk_df = pd.read_csv("../results/vtk-eval/vtk-eval.csv", comment="#")
            vcnt_df = pd.read_csv("../results/image-eval/image-eval.csv", comment="#")
            vcnt_df = vcnt_df[vcnt_df['Shading Mode'] == "local"]   # VTK only supports local shading
            data_df = pd.read_csv("../results/csgv-eval/csgv-eval.csv", comment="#")
            # only use VTK evaluated data sets and shading mode
            if ONLY_VTK_DATA:
                vcnt_df = vcnt_df[vcnt_df['Data Set'].isin(vtk_df['Data Set'])]
                data_df = data_df[data_df['Data Set'].isin(vtk_df['Data Set'])]
            data_df["Voxels"] = data_df["DimX"] * data_df["DimY"] * data_df["DimZ"]
            data_df["Labels/Voxels"] = data_df["Labels"] / data_df["Voxels"]

            # create bar plots
            data_df = data_df.sort_values(by=SECOND_VALUE_KEY)
            data_sets = data_df["Data Set"]
            x = np.arange(len(data_sets))

            fig, ax = plot_timings_grouped(x, [vtk_df.set_index("Data Set").reindex(data_sets).reset_index()["frame avg [ms]"],
                                               vcnt_df.set_index("Data Set").reindex(data_sets).reset_index()["frame avg [ms]"]],
                                           ylabel="$\varnothing$ Frame Time [ms]", xticklabels=map(get_data_set_tex, data_sets),
                                           labels=["VTK", "Volcanite"], barlabelfmt="%.1f")

            # Add secondary y-axis on right for data set sizes
            ax2 = ax.twinx()
            ax2.bar(x, data_df.set_index("Data Set").reindex(data_sets).reset_index()[SECOND_VALUE_KEY], color=volcanite_colors_dark[2], zorder=1, alpha=0.3, width=0.9)
            ax2.set_ylabel(f"Number of {SECOND_VALUE_KEY}", color=volcanite_colors_dark[2])
            ax2.tick_params(axis='y', labelcolor=volcanite_colors_dark[2])
            if SECOND_VALUE_LOG:
                ax2.set_yscale('log')

            # move alternative axis behind
            ax2.set_zorder(1)
            ax.set_zorder(2)
            ax2.patch.set_visible(True)
            ax.patch.set_visible(False)

            #fig.tight_layout()

            save_plot(f"../results/plts/vtk-img-timings_{SECOND_VALUE_KEY.lower()}{"-log" if SECOND_VALUE_LOG else ""}{"" if ONLY_VTK_DATA else "_all"}.pdf", fig)
