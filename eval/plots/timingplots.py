import itertools

import numpy as np
import pandas as pd
import matplotlib.pyplot as plt

from common import *

init_plots()

def plot_gpu_timings(df : pd.DataFrame, stages : list[str], xlabel : str | None = None,
                     legendstages : list [str] | None  = None, legend : bool = True, frames : int = 16,
                     framestep : int = 1, show : bool = False) -> plt.Figure:

    if legendstages is None:
        legendstages = stages

    # constant parameters
    width = 0.66

    plt.figure()
    fig, ax = plt.subplots(layout='constrained', figsize=(9, 3))
    cycler = itertools.cycle(plt.rcParams['axes.prop_cycle'])

    x = np.arange(frames)
    bottom = np.zeros_like(x, dtype='float64')
    for i,s in enumerate(stages):
        y = [df[s][f] for f in x]

        edgecolor = next(cycler)['edgecolor']
        ax.bar(x, y, width=width, bottom=bottom, label=legendstages[i], edgecolor=edgecolor, zorder=3)
        bottom += y

    ax.grid(axis='y', color='gray', alpha=0.5, linewidth=0.5, zorder=0)
    plt.xticks(x)
    if xlabel:
        plt.xlabel(xlabel)
    plt.ylabel("Frame time [ms]", fontsize=14)
    if legend:
        plt.legend(ncol=len(stages), loc='upper right')
    plt.tight_layout()

    if show:
        plt.show()

    return fig
