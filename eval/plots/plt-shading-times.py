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

from timingplots import plot_timings, add_fps_twin_axis_for_ms_axis
from common import *

import pandas as pd
from pathlib import Path
import matplotlib.pyplot as plt


# image rendering (no camera movement)
print("--------------\nPlotting Shading Timings")
init_plots()

x = np.arange(4)
avg_ms = np.zeros_like(x, dtype=float)
min_ms = np.zeros_like(x, dtype=float)
max_ms = np.zeros_like(x, dtype=float)
sdv_ms = np.zeros_like(x, dtype=float)

color = ["#999999","#ffd068", "#6c8ebf", "#b85450"]
edgecolor = ["#000000", "#000000", "#000000", "#000000"]

shading_csv = Path("../results/image-eval/image-eval.csv")
df = pd.read_csv(shading_csv, comment="#")

for data in data_set_ids:

    print(f"  Plotting shading times for {data}")

    for i, shading in enumerate(shading_mode_ids):
        avg_ms[i] = df[(df['Data Set'] == data) & (df['Shading Mode'] == shading)]['frame avg [ms]'].iloc[0]
        min_ms[i] = df[(df['Data Set'] == data) & (df['Shading Mode'] == shading)]['frame min [ms]'].iloc[0]
        max_ms[i] = df[(df['Data Set'] == data) & (df['Shading Mode'] == shading)]['frame max [ms]'].iloc[0]
        sdv_ms[i] = df[(df['Data Set'] == data) & (df['Shading Mode'] == shading)]['stdv'].iloc[0]

    fig, ax = plt.subplots(constrained_layout=True, figsize=(5,4))
    plot_timings(x, avg_ms, errors=sdv_ms, xticklabels=shading_mode_ids,
                 color=color, edgecolor="#000000", ylabel="Average frame time [ms]", fig=fig, ax=ax)

    # Twin y-axis: frames per second (computed, but no bars plotted)
    add_fps_twin_axis_for_ms_axis(fig, ax, color='gray')

    save_plot(f"../results/plots/shading-img-timings_{data}.pdf", fig)

    ax.clear()
    plt.clf()
    plt.close('all')
