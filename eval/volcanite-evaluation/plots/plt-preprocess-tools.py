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

print("--------------\nPlotting Neuroglancer\\VTK\\Volcanite Preprocessing")
init_plots()


def whiten(c, w=0.2):
    if isinstance(c, list):
        for i, _c in enumerate(c):
            c[i] = _c[0] * (1. - w) + w, _c[1] * (1. - w) + w, _c[2] * (1. - w) + w
        return c
    else:
        return c[0] * (1. - w) + w, c[1] * (1. - w) + w, c[2] * (1. - w) + w


# config
csgv_col = volcanite_colors[0]
vtk_col = volcanite_colors[1]
ng_col = volcanite_colors[4]
csgv_col_dark = volcanite_colors_dark[0]
vtk_col_dark = volcanite_colors_dark[1]
ng_col_dark = volcanite_colors_dark[4]
barwidth = 0.2

for ONLY_MUTUAL_DATA in [False, True]:
    for SCALE in ['linear', 'log']:
        for SECOND_AXIS in [False, True]:

            print(f"  Plotting preprocessing {"mutual" if ONLY_MUTUAL_DATA else "any"} data"
                  f" ({SCALE}){" with sizes" if SECOND_AXIS else ""}")

            # load data
            csgv_df = pd.read_csv("../results/compression-eval/compression-eval.csv", comment="#")
            csgv_df["Voxels"] = csgv_df["DimX"] * csgv_df["DimY"] * csgv_df["DimZ"]
            csgv_df["Labels/Voxels"] = csgv_df["Labels"] / csgv_df["Voxels"]

            vtk_df = pd.read_csv("../results/vtk-eval/vtk-eval.csv", comment="#")
            vtk_df["preprocess time [s]"] = np.nan      # vtk has no preprocessing time without IO
            vtk_df["Total Size [GB]"] = np.nan          # TODO: obtain total sizes (gzip) and no gzip [GB] for VTK?

            ng_df = pd.read_csv("../results/neuroglancer-eval/neuroglancer-eval.csv", comment="#")
            ng_df["preprocess IO time [s]"] = ng_df["Precomputed Time [s]"] + ng_df["Meshing Time [s]"]
            ng_df["preprocess time [s]"] = np.nan       # neuroglancer has no preprocessing time without IO
            ng_df["time to first frame [s]"] = np.nan   # neuroglancer has no time fo first frame
            ng_df["Total Size (gzip) [GB]"] = ng_df["Mesh Size (gzip) [GB]"] + ng_df["Precomputed Size (gzip) [GB]"]
            ng_df["Total Size [GB]"] = ng_df["Mesh Size [GB]"] + ng_df["Precomputed Size [GB]"]

            # VTK: preprocess w. IO, time to first frame
            # Neuroglancer: preprocess volume w. IO, preprocess mesh w. IO
            # Volcanite: preprocess, preprocess w. IO, time to first frame


            csgv_df = csgv_df.sort_values(by="Orig Size [GB]")
            if ONLY_MUTUAL_DATA:
                csgv_df = csgv_df[csgv_df['Data Set'].isin(vtk_df['Data Set'])]
                csgv_df = csgv_df[csgv_df['Data Set'].isin(ng_df['Data Set'])]
            data_sets = csgv_df["Data Set"]
            x = np.arange(len(data_sets))

            figwidth = 10 if ONLY_MUTUAL_DATA else 14
            fig, ax = plt.subplots(constrained_layout=True, figsize=(figwidth, 4))

            # overlay multiple bar plots (start with the highest values and plot lower values above)
            # time to first frame, preprocess, preprocess with IO
            # plot_timings_grouped(x, [csgv_df.set_index("Data Set").reindex(data_sets).reset_index()["Time To First Frame [s]"],
            #                          vtk_df.set_index("Data Set").reindex(data_sets).reset_index()["time to first frame [s]"],
            #                          ng_df.set_index("Data Set").reindex(data_sets).reset_index()["time to first frame [s]"],
            #                          ],
            #                      colors=["white", "white", "white"], barwidth=barwidth,
            #                      edgecolors=[csgv_col_dark, vtk_col_dark, ng_col_dark],
            #                      ylabel=r"Time To First Frame [s]", xticklabels=map(get_data_set_tex, data_sets), marknan=False,
            #                      fig=fig, ax=ax)
            plot_timings_grouped(x, [csgv_df.set_index("Data Set").reindex(data_sets).reset_index()["Compression Time Total with IO [s]"],
                                     vtk_df.set_index("Data Set").reindex(data_sets).reset_index()["preprocess IO time [s]"],
                                     ng_df.set_index("Data Set").reindex(data_sets).reset_index()["preprocess IO time [s]"],
                                     ],
                                 colors=whiten([csgv_col, vtk_col, ng_col]), barwidth=barwidth,
                                 edgecolors=[csgv_col_dark, vtk_col_dark, ng_col_dark],
                                 ylabel=r"Preprocess (with IO)", xticklabels=map(get_data_set_tex, data_sets),
                                 marknan=True, marknan_offset=(0.01 if SCALE == 'log' else 0.25),
                                 fig=fig, ax=ax)
            # plot_timings_grouped(x, [csgv_df.set_index("Data Set").reindex(data_sets).reset_index()["Compression Time Total [s]"],
            #                          vtk_df.set_index("Data Set").reindex(data_sets).reset_index()["preprocess time [s]"],
            #                          ng_df.set_index("Data Set").reindex(data_sets).reset_index()["preprocess time [s]"],
            #                          ],
            #                      colors=whiten([csgv_col, vtk_col, ng_col], 0.5), barwidth=barwidth,
            #                      edgecolors=[csgv_col_dark, vtk_col_dark, ng_col_dark],
            #                      ylabel=r"Preprocess (no IO)", xticklabels=map(get_data_set_tex, data_sets), marknan=False,
            #                      fig=fig, ax=ax)

            ax.set_yscale(SCALE)
            ax.set_ylabel("Preprocessing Time [s]")

            legend = ax.legend([Patch(facecolor=csgv_col, edgecolor=csgv_col_dark, label='Volcanite'),
                                Patch(facecolor=vtk_col, edgecolor=csgv_col_dark, label='VTK'),
                                Patch(facecolor=ng_col, edgecolor=csgv_col_dark, label='Neuroglancer'),
                                # Patch(facecolor='#FFFFFF', edgecolor='#333333', label='Time To First Frame'),
                                # Patch(facecolor='#888888', edgecolor='#333333', label='Compression (w. IO)'),
                                # Patch(facecolor='#CCCCCC', edgecolor='#333333', label='Compression (no IO)'),
                                ],
                                 ["Volcanite", "VTK", "Neuroglancer",
                                  # "Time to First Frame", "with IO", "without IO"
                                 ],
                                 loc='upper left', frameon=True, ncols=3,
                                 handlelength=1.0,
                                 handleheight=1.0,
                                 handletextpad=0.2,
                                 columnspacing=0.4
                                 )

            # Add secondary y-axis on right for compressed data set sizes
            if SECOND_AXIS:
                ax2 = ax.twinx()

                #background_colors = (whiten(csgv_col), whiten(vtk_col), whiten(ng_col))
                background_colors = ('gray', 'gray', 'gray')

                xoffset = -(3 - 1) * barwidth * 1 / 2
                for i,y in enumerate([csgv_df.set_index("Data Set").reindex(data_sets).reset_index()["CSGV Size [GB]"],
                                      vtk_df.set_index("Data Set").reindex(data_sets).reset_index()["Total Size [GB]"],
                                      ng_df.set_index("Data Set").reindex(data_sets).reset_index()["Total Size (gzip) [GB]"]]):

                    ax2.scatter(x + xoffset + i * barwidth, y, color=background_colors[i], marker=11, s=50)

                                     # colors=[background_colors[0], background_colors[1], background_colors[2]],
                                     # edgecolors=[background_colors[0], background_colors[1], background_colors[2]],
                                     # ylabel=r"Size on Disk", xticklabels=map(get_data_set_tex, data_sets), marknan=False,
                                     # fig=fig, ax=ax2)

                ax2.set_ylabel(r"Size on Disk [GB] $\blacktriangleleft$", color='gray')
                ax2.tick_params(axis='y', labelcolor='gray')
                ax2.set_yscale(SCALE)

            save_plot(f"../results/plots/preprocessing-tools{"" if ONLY_MUTUAL_DATA else "_all"}_{SCALE}{"_size" if SECOND_AXIS else ""}.pdf", fig)

