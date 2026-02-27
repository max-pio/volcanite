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

from timingplots import plot_gpu_timings, add_fps_twin_axis_for_ms_axis
from common import *

import pandas as pd
from pathlib import Path
import matplotlib.pyplot as plt



# image rendering (no camera movement)
print("--------------\nPlotting GPU Timings")
init_plots()

for cam in ["image", "video"]:
    for data in data_set_ids:
        for shading in shading_mode_ids:

            gpu_csv = Path(f"../results/{cam}-eval/{data}_{shading}_timing.csv")

            if gpu_csv.exists():
                print(f"  Plotting {cam} {data} {shading}")

                df = pd.read_csv(gpu_csv, comment="#")
                df["Other"] = df["Total"] - df["Cache"] - df["Decompress"] - df["Render"] - df["Post-Process"]

                fig, ax = plt.subplots(constrained_layout=True, figsize=(8, 2))
                if cam == "image":
                    plot_gpu_timings(df, x=np.arange(21),
                                     stages=["Cache","Decompress","Render","Post-Process","Other"],
                                     xlabel="Image Frame", ylabel="Frame Time [ms]",
                                     fig=fig, ax=ax)
                    ax.legend(ncols=3)
                else:
                    plot_gpu_timings(df, x=np.arange(0, 600, 20),
                                     stages=["Cache", "Decompress", "Render", "Post-Process", "Other"],
                                     xlabel="Video Frame", ylabel="Frame Time [ms]", barwidth=(20 * 0.66),
                                     fig=fig, ax=ax)
                    ax.set_xticks(np.arange(0, 600, 40))
                    ax.legend(loc="upper left")

                add_fps_twin_axis_for_ms_axis(fig, ax, labelpad=-5)

                # save once with legend
                save_plot(f"../results/plots/gpu-{cam}-timings_{data}_{shading}_leg.pdf", fig)

                # save once without legend
                ax.legend()
                ax.get_legend().remove()
                # special case for images without legends: shorter width to put image next to plot in document
                if cam == "image":
                    width, height = fig.get_size_inches()
                    fig.set_figwidth(0.75 * width)
                save_plot(f"../results/plots/gpu-{cam}-timings_{data}_{shading}.pdf", fig)

                # store for separated legend plot
                handles, _ = ax.get_legend_handles_labels()

                ax.clear()
                plt.clf()
                plt.close('all')



# Create new figure with ONLY the legend
legend_path = Path(f"../results/plots/gpu-timings-legend.pdf")
print(f"  Plotting gpu timings legend")
fig_legend = plt.figure(constrained_layout=True)  # Adjust size as needed

# Create custom patch: white fill, black edge
legend = fig_legend.legend(handles,
                           ["Cache", "Decompress", "Render", "Post-Process", "Other"],
                           loc='center', frameon=True, ncols=5,
                           # handlelength=1.0,
                           # handleheight=1.0,
                           # handletextpad=0.2,
                           # columnspacing=0.4
                           )

save_plot(legend_path, fig_legend)
plt.close(fig_legend)
