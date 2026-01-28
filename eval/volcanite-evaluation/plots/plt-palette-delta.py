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

from matplotlib.patches import Patch

from timingplots import plot_timings_grouped
from common import *

import numpy as np
import pandas as pd

print("--------------\nPlotting Palette Delta Sizes")
init_plots()

ONLY_RELEVANT_DATA = True
X_SORT_KEY = "Orig Size [GB]"
Y_KEY = "Palette Length max"

# load data
palette_df = pd.read_csv("../results/deltaoperation-b64-eval/deltaoperation-b64-eval.csv", comment="#")
data_df = pd.read_csv("../results/csgv-eval/csgv-eval.csv", comment="#")

# palette length evaluation is only relevant for data with many labels
if ONLY_RELEVANT_DATA:
    #palette_df = palette_df[~palette_df['Data Set'].isin(["pa66", "Griesser2022-sample", "xtm-battery", ""])]
    palette_df = palette_df[(palette_df['Labels'] > 255)]

data_df = data_df[data_df['Data Set'].isin(palette_df['Data Set'])]
data_df["Voxels"] = data_df["DimX"] * data_df["DimY"] * data_df["DimZ"]
data_df["Labels/Voxels"] = data_df["Labels"] / data_df["Voxels"]

# create bar plots
data_df = data_df.sort_values(by=X_SORT_KEY)
data_sets = data_df["Data Set"]
x = np.arange(len(data_sets))

ys = []
for op_delta in ["","d-","d"]:
    if op_delta == "":
        df = palette_df[palette_df['Unlimited Pdelta'].isna() | palette_df['Unlimited Pdelta'].eq("")]
    else:
        df = palette_df[palette_df['Unlimited Pdelta'].eq(op_delta)]
    ys = ys + [df.set_index("Data Set").reindex(data_sets).reset_index()[Y_KEY]]

fig, ax = plt.subplots(constrained_layout=True, figsize=(4, 4))
plot_timings_grouped(x, ys,
                     ylabel=r"max $\lvert P \rvert$", xticklabels=map(get_data_set_tex, data_sets),
                     labels=["None", r"Lim. $\; \; \cdot$", r"Unlim. $\; \; \; \cdot$"],
                     marknan=False, barwidth=0.3, fig=fig, ax=ax)


# add the palette delta images to the legend
import matplotlib.image as mpimg
from matplotlib.offsetbox import OffsetImage, AnnotationBbox
image = mpimg.imread("./img/op_palette_delta.png")
imagebox = OffsetImage(image, zoom=0.1, cmap='gray')
ax.add_artist(AnnotationBbox(imagebox, (0.40, 0.83), frameon=False, boxcoords="axes fraction", zorder=10))
image = mpimg.imread("./img/op_palette_delta_old.png")
imagebox = OffsetImage(image, zoom=0.1, cmap='gray')
ax.add_artist(AnnotationBbox(imagebox, (0.46, 0.745), frameon=False, boxcoords="axes fraction", zorder=10))


save_plot("../results/plots/palette-delta.pdf", fig)
plt.close(fig)
