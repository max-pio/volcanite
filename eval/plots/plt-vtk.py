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

init_plots()

# load data
vtk_df = pd.read_csv("../results/vtk-eval/vtk-eval.csv", comment="#")
vcnt_df = pd.read_csv("../results/image-eval/image-eval.csv", comment="#")
# only use VTK evaluated data sets and shading mode
vcnt_df = vcnt_df[vcnt_df['Data Set'].isin(vtk_df['Data Set'])]
vcnt_df = vcnt_df[vcnt_df['Shading Mode'] == "local"]

# create bar plots

data_sets = vtk_df["Data Set"].unique()
x = np.arange(len(data_sets))

fig = plot_timings_grouped(x, [vtk_df.set_index("Data Set")["frame avg [ms]"], vcnt_df.set_index("Data Set")["frame avg [ms]"]],
                   ylabel="Average frame time [ms]", xticklabels=map(get_data_set_tex, data_sets), labels=data_sets, barlabelfmt="%.1f")


save_plot("../results/vtk-eval/vtk-image-timings.pdf", fig)
