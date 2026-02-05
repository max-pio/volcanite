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

from timingplots import plot_gpu_timings
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

                if cam == "image":
                    fig, ax = plot_gpu_timings(df, x=np.arange(21),
                                               stages=["Cache","Decompress","Render","Post-Process","Other"],
                                               xlabel="Image Frame", ylabel="Frame Time [ms]")
                    ax.legend(ncols=3)
                else:
                    fig, ax = plot_gpu_timings(df, x=np.arange(0, 600, 20),
                                               stages=["Cache", "Decompress", "Render", "Post-Process", "Other"],
                                               xlabel="Video Frame", ylabel="Frame Time [ms]", barwidth=(20 * 0.66))
                    ax.set_xticks(np.arange(0, 600, 40))
                    ax.legend()
                    ax.get_legend().remove()
                save_plot(f"../results/plots/gpu-{cam}-timings_{data}_{shading}.pdf", fig)

                ax.clear()
                plt.clf()
                plt.close('all')
