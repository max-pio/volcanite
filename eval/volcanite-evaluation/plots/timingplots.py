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
from cycler import cycler

from common import *

init_plots()

def plot_gpu_timings(df : pd.DataFrame, x, stages : list[str], xlabel : str | None = None, ylabel : str | None = None,
                     legendstages : list [str] | None  = None, legend : bool = True,
                     framestep : int = 1, barwidth = 0.66, xticklabels = None,
                     fig = None, ax = None) -> (plt.Figure, plt.Axes):

    if legendstages is None:
        legendstages = stages

    if fig is None or ax is None:
        plt.figure()
        fig, ax = plt.subplots(constrained_layout=True, figsize=(8, 2.5))

    bottom = np.zeros_like(x, dtype='float64')
    for i,s in enumerate(stages):
        y = [df[s][f] for f in x]

        edgecolor = volcanite_colors_dark[i]
        color = volcanite_colors[i]

        ax.bar(x, y, width=barwidth, bottom=bottom, label=legendstages[i], color=color, edgecolor=edgecolor, zorder=3)
        bottom += y

    ax.grid(axis='y', color='gray', alpha=0.5, linewidth=0.5, zorder=0)
    plt.xticks(x, xticklabels)
    if xlabel:
        plt.xlabel(xlabel)
    if ylabel:
        plt.ylabel(ylabel, fontsize=14)
    if legend:
        plt.legend(ncol=len(stages), loc='upper right')

    return fig, ax

def plot_timings(x, y,
                 xlabel = None, ylabel = None,
                 xticklabels = None, color = None, edgecolor = None, barwidth=0.3, errors = None,
                 fig = None, ax = None) -> plt.Figure:

    if fig is None or ax is None:
        plt.figure()
        fig, ax = plt.subplots(constrained_layout=True, figsize=(3,2))

    ax.bar(x, y, yerr=errors, capsize=8, width=barwidth, linewidth=1, edgecolor=(edgecolor if edgecolor else volcanite_colors_dark[0]),
           color=(color if color else volcanite_colors[0]), zorder=3)

    ax.grid(axis='y', color='gray', alpha=0.5, linewidth=0.5, zorder=0)
    if not xticklabels is None:
        ax.set_xticks(x, xticklabels, rotation=45, ha='right')
    if xlabel:
        plt.xlabel(xlabel, fontsize=14)
    if ylabel:
        plt.ylabel(ylabel, fontsize=14)

    #plt.tight_layout()
    #plt.show()

    return fig, ax

def plot_timings_grouped(x, ys : list, labels : np.ndarray | list[str] | None = None,
                         xlabel = None, ylabel = None, xticklabels = None, colors = None, edgecolors = None,
                         barlabelfmt : str | None = None, marknan : bool = True, barwidth = None, baroffsetscale = 1.,
                         fig = None, ax = None) -> (plt.Figure, plt.Axes):


    if fig is None or ax is None:
        plt.figure()
        fig, ax = plt.subplots(constrained_layout=True, figsize=(4,4))

    count = len(ys)
    if barwidth is None:
        barwidth = 1. / count - 0.05
    xoffset = -(count - 1) * barwidth * baroffsetscale / 2

    for i, y in enumerate(ys):
        if edgecolors is None:
            edgecolor = volcanite_colors_dark[i]
        else:
            edgecolor = edgecolors[i]

        if colors is None:
            color = volcanite_colors[i]
        else:
            color = colors[i]

        if labels is None:
            label = None
        else:
            label = labels[i]

        bars = ax.bar(x + xoffset + i * barwidth * baroffsetscale,
                      y, barwidth, label=label, linewidth=1, zorder=3,
                      edgecolor=edgecolor, color=color)
        if barlabelfmt:
            ax.bar_label(bars, padding=2, fmt=barlabelfmt, size=9)

    # add ticks and colors
    ax.grid(axis='y', color='gray', alpha=0.5, linewidth=0.5, zorder=0)
    if xticklabels:
        ax.set_xticks(x, xticklabels, rotation=45, ha='right')
    if xlabel:
        plt.xlabel(xlabel, fontsize=14)
    if ylabel:
        plt.ylabel(ylabel, fontsize=14)

    if labels:
        ax.legend(labels=labels, loc='upper left', ncol=1)

    # add markers for non-existing values
    if marknan:
        nan_marker_height = (3 * ax.get_ylim()[0] + ax.get_ylim()[1]) / 4
        for i, y in enumerate(ys):
            if edgecolors is None:
                edgecolor = volcanite_colors_dark[i]
            else:
                edgecolor = edgecolors[i]

            if colors is None:
                color = volcanite_colors[i]
            else:
                color = colors[i]

            # Add markers for non-existing entries
            # filter NaN positions (non-existing data)
            nan_mask = pd.isna(y)
            x_nan = x[nan_mask.values] + xoffset + i * barwidth * baroffsetscale
            # plot X markers at bottom of
            # for j in x:
            ax.scatter(x_nan, np.full_like(x_nan, nan_marker_height),
                       marker='X', s=(barwidth * barwidth * 500), color=color, edgecolor=edgecolor,
                       label='N/A', zorder=5)

    return fig, ax


def add_fps_twin_axis_for_ms_axis(fig, ax, color='gray', labelpad=-10):
    """For a Y axis plot measuring [ms], adds another Y axis that shows the FPS for each y tick."""

    fig.canvas.draw()
    ax2 = ax.twinx()
    ticks = ax.get_yticks()
    ticks = ticks[ticks > 0]
    ax2.set_yticks(ticks)
    ax2.set_ylim(ax.get_ylim())
    ax2.tick_params(axis='y', colors=color)
    ax2.set_yticklabels([f'{1000. / t:.0f}%' for t in ticks], color=color)
    ax2.set_ylabel("FPS", color=color, labelpad=labelpad)
    ax2.yaxis.label.set_position((0, 0.04))
    ax2.yaxis.label.set_rotation(0)