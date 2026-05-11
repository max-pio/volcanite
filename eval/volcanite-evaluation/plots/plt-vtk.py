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

            # fig, ax = plt.subplots(constrained_layout=True, figsize=(6, 4))
            # plot_timings_grouped(x, [vcnt_df.set_index("Data Set").reindex(data_sets).reset_index()["frame avg [ms]"],
            #                          vtk_df.set_index("Data Set").reindex(data_sets).reset_index()["frame avg [ms]"]],
            #                      ylabel=r"Avg. Frame Time [ms]", xticklabels=map(get_data_set_tex, data_sets),
            #                      labels=["Volcanite", "VTK"], barlabelfmt="%.1f", fig=fig, ax=ax)

            # Replace single subplot with dual subplots
            height_ratios = [0.2, 0.8]
            fig, (ax_outliers, ax_main) = plt.subplots(2, 1, sharex=True,
                                                       constrained_layout=True,
                                                       figsize=(10, 4), height_ratios=height_ratios)

            # plot lower half of plot (main axis)
            plot_timings_grouped(x, [vcnt_df.set_index("Data Set").reindex(data_sets).reset_index()["frame avg [ms]"],
                                         vtk_df.set_index("Data Set").reindex(data_sets).reset_index()["frame avg [ms]"]],
                                 ylabel=r"Avg. Frame Time [ms]", xticklabels=map(get_data_set_tex, data_sets),
                                 labels=None, barlabelfmt="%.1f", barwidth=0.4,
                                 marknan=True, marknan_offset=0.025,
                                 fig=fig, ax=ax_main)

            # BEGIN SECONDARY AXIS
            # Add secondary y-axis on right for data set sizes
            ax2 = ax_main.twinx()
            ax2.bar(x, data_df.set_index("Data Set").reindex(data_sets).reset_index()[SECOND_VALUE_KEY],
                    color='gray', zorder=1, alpha=0.3, width=0.9)
            ax2.set_ylabel(f"Number of {SECOND_VALUE_KEY}", color='gray')
            ax2.tick_params(axis='y', labelcolor='gray')
            if SECOND_VALUE_LOG:
                ax2.set_yscale('log')

            # move alternative axis behind
            ax2.set_zorder(1)
            ax_main.set_zorder(2)
            ax2.patch.set_visible(True)
            ax_main.patch.set_visible(False)
            # END SECONDARY AXIS


            # plot upper half of plot (outliers axis)
            plot_timings_grouped(x, [vcnt_df.set_index("Data Set").reindex(data_sets).reset_index()["frame avg [ms]"],
                                        vtk_df.set_index("Data Set").reindex(data_sets).reset_index()["frame avg [ms]"]],
                                 ylabel="", xticklabels=map(get_data_set_tex, data_sets),
                                 marknan=False,
                                 labels=["Volcanite", "VTK"], barlabelfmt="%.1f",
                                 fig=fig, ax=ax_outliers)
            ax_outliers.legend(fontsize=12, loc="upper left", ncol=1)

            # make bar labels smaller
            for text in ax_main.texts:
                text.set_fontsize(7)
            for text in ax_outliers.texts:
                text.set_fontsize(7)

            # Configure y-limits: main data (0-50), outliers (350-max)
            ax_main.set_ylim(0, 50)
            ax_outliers.set_ylim(350, 1450)

            # Hide connecting spines and adjust ticks
            ax_outliers.spines['bottom'].set_visible(False)
            ax_main.spines['top'].set_visible(False)
            ax2.spines['top'].set_visible(False)
            ax_outliers.xaxis.tick_top()
            ax_outliers.tick_params(labeltop=False)

            # Add diagonal break lines
            d = 0.015
            kwargs = dict(transform=ax_main.transAxes, color='k', linewidth=1.5, clip_on=False)
            ax_main.plot((-d, +d), (1 - d, 1 + d), **kwargs)  # Bottom axis: up to right
            ax_main.plot((1 - d, 1 + d), (1 - d, 1 + d), **kwargs)
            h = height_ratios[1] / height_ratios[0]
            kwargs.update(transform=ax_outliers.transAxes)
            ax_outliers.plot((-d, +d), (-d*h, +d*h), **kwargs)  # Top axis: down to right
            ax_outliers.plot((1 - d, 1 + d), (-d*h, +d*h), **kwargs)


            save_plot(f"../results/plots/vtk-image-timings_{SECOND_VALUE_KEY.lower()}"
                      f"{"-log" if SECOND_VALUE_LOG else ""}{"" if ONLY_VTK_DATA else "_all"}.pdf", fig)
