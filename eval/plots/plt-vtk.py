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

import itertools

import numpy as np
import pandas as pd
import matplotlib.pyplot as plt

from common import *

init_plots()

# load data
vtk_df = pd.read_csv("../results/vtk-eval/vtk-eval.csv", comment="#")
vcnt_df = pd.read_csv("../results/image-eval/image-eval.csv", comment="#")
# only use VTK evaluated data sets and shading mode
vcnt_df = vcnt_df[vcnt_df['Data Set'].isin(vtk_df['Data Set'])]
vcnt_df = vcnt_df[vcnt_df['Shading Mode'] == "local"]

# create bar plots
width = 0.45
data_sets = vtk_df["Data Set"].unique()

plt.figure(figsize=(4, 4))
fig, ax = plt.subplots(layout='constrained')
cycler = itertools.cycle(plt.rcParams['axes.prop_cycle'])

x = np.arange(len(data_sets))
edgecolor = next(cycler)['edgecolor']
bars1 = ax.bar(x - width/2, vtk_df.set_index("Data Set")["frame avg [ms]"], width, label='VTK', linewidth=1, edgecolor=edgecolor)
ax.bar_label(bars1, padding=2, fmt="%.1f", size=11)
edgecolor = next(cycler)['edgecolor']
bars2 = ax.bar(x + width/2, vcnt_df.set_index("Data Set")["frame avg [ms]"], width, label='Volcanite', linewidth=1, edgecolor=edgecolor)
ax.bar_label(bars2, padding=2, fmt="%.1f", size=11)

ax.set_xticks(x, map(get_data_set_tex, data_sets), rotation=45, ha='right')

plt.ylabel("Average frame time [ms]", fontsize=14)
plt.tight_layout()
plt.show()

save_plot("../results/vtk-eval/vtk-image-timings.pdf", fig)
