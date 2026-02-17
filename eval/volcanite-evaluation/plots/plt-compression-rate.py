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
print("--------------\nPlotting CSGV Compression Rates")
init_plots()

X_SORT_KEY = "Orig Size [GB]"

csgv_csv = Path("../results/csgv-eval/csgv-eval.csv")
# load data
data_df = pd.read_csv(csgv_csv, comment="#")
data_df["Voxels"] = data_df["DimX"] * data_df["DimY"] * data_df["DimZ"]
data_df["Labels/Voxels"] = data_df["Labels"] / data_df["Voxels"]


data_df = data_df.sort_values(by=X_SORT_KEY)
data_sets = data_df["Data Set"]
x = np.arange(len(data_sets))


print("  Plotting compression rates")

fig, ax = plt.subplots(figsize=(10, 2.5))
plot_timings(x, data_df.set_index("Data Set").reindex(data_sets).reset_index()["Compression Rate [Pcnt]"],
             xticklabels=map(get_data_set_tex, data_sets), color=volcanite_colors[0],
             barwidth=0.6, ylabel=r"Compression Rate [\%]",
             fig=fig, ax=ax)

save_plot("../results/plots/csgv-compression-rates.pdf", fig)
plt.close(fig)

for l in ["linear", "log"]:
    print(f"  Plotting orig. and csgv sizes {l}")

    fig, ax = plt.subplots(figsize=(10, 2.5))
    plot_timings_grouped(x, [data_df.set_index("Data Set").reindex(data_sets).reset_index()["Orig Size [GB]"],
                                 data_df.set_index("Data Set").reindex(data_sets).reset_index()["CSGV Size [GB]"]],
                         ylabel="Size [GB]", xticklabels=map(get_data_set_tex, data_sets),
                         labels=["Original", "CSGV"], marknan=False, barwidth=0.5, baroffsetscale=0.4, fig=fig, ax=ax)
    ax.set_yscale(l)

    save_plot(f"../results/plots/csgv-compression-sizes_{l}.pdf", fig)
    plt.close(fig)
