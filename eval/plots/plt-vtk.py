import itertools

import numpy as np
import pandas as pd
import matplotlib.pyplot as plt

from plots.common import *

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

plt.figure(figsize=(3, 3))
fig, ax = plt.subplots(layout='constrained')
cycler = itertools.cycle(plt.rcParams['axes.prop_cycle'])
print(vtk_df.set_index("Data Set")["frame avg [ms]"])

x = np.arange(len(data_sets))
edgecolor = next(cycler)['edgecolor']
bars1 = ax.bar(x - width/2, vtk_df.set_index("Data Set")["frame avg [ms]"], width, label='VTK', linewidth=1, edgecolor=edgecolor)
edgecolor = next(cycler)['edgecolor']
bars2 = ax.bar(x + width/2, vcnt_df.set_index("Data Set")["frame avg [ms]"], width, label='Volcanite', linewidth=1, edgecolor=edgecolor)

ax.set_xticks(x, data_sets, rotation=45, ha='right')

plt.ylabel("Average frame time [ms]")
plt.tight_layout()
plt.show()