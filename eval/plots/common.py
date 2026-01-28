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

import pathlib
import matplotlib.pyplot as plt
from matplotlib import rc, colormaps
from matplotlib.colors import ListedColormap


# create Volcanite colormap and register with matplotlib
volcanite_colors = [
    (128/255, 177/255, 211/255),  # volcanitecol5: light blue #80B1D3
    (255/255, 255/255, 179/255),  # volcanitecol2: light yellow #FFFFB3
    (251/255, 128/255, 114/255),  # volcanitecol4: light red #FB8072
    (179/255, 222/255, 105/255),  # volcanitecol7: light green #B3DE69
    (190/255, 186/255, 218/255),  # volcanitecol3: light purple #BEBADA
    (141/255, 211/255, 199/255),  # volcanitecol1: light teal #8DD3C7
    (253/255, 180/255,  98/255),  # volcanitecol6: orange #FDB462
]
volcanite_colors_dark = [(r * 0.6, g * 0.6, b * 0.6) for (r,g,b) in volcanite_colors]
volcanite_cmap = ListedColormap(volcanite_colors)
colormaps.register(volcanite_cmap, name='volcanite_cmap')

# setup default plot configurations
def init_plots():
    plt.rcParams["text.usetex"] = True
    plt.rcParams["text.latex.preamble"] = r"\usepackage{amsmath}"
    plt.rcParams["font.family"] = "serif"
    plt.rcParams["font.serif"] = "Computer Modern Roman"
    plt.rcParams["font.size"] = 14
    #rc('axes', prop_cycle=(cycler(color=volcanite_colors, edgecolor=volcanite_colors_dark)))
    # rc("image", cmap="volcanite_cmap")


# data set names and labels
data_set_ids = ["Ara2016", "azba", "cells", "fiber", "Griesser2022-sample", "Griesser2022-validation",
                "H01-bloodvessel", "H01-wm", "liconn", "Motta2019-small", "Motta2019", "pa66", "Wolny2020",
                "xtm-battery"]
shading_mode_ids = ["local", "shadow", "ao", "pt"]

def get_data_set_tex(dataset : str):
    _dataset2tex = {"pa66": "\\textsc{Polyamid}",
                    "Ara2016": "\\textsc{MouseBA}",
                    "Griesser2022-sample": "\\textsc{Fabric}",
                    "Griesser2022-validation": "\\textsc{Fabric$_{\\textsc{val}}$}",
                    "Wolny2020": "\\textsc{Plant}",
                    "xtm-battery": "\\textsc{Battery}",
                    "azba": "\\textsc{AZBA}",
                    "H01-bloodvessel": "\\textsc{H01$_{\\textsc{BV}}$}",
                    "H01-wm": "\\textsc{H01$_{\\textsc{WM}}$}",
                    "liconn": "\\textsc{Liconn}",
                    "Motta2019-small": "\\textsc{MouseL4$_\\textsc{S}$}",
                    "Motta2019": "\\textsc{MouseL4}",
                    "cells": "\\textsc{Cells}",
                    "fiber": "\\textsc{Fiber}",
                    "celegans": "\\textsc{CElegans}",
                    }
    return _dataset2tex[dataset]

def save_plot(file : pathlib.Path | str, fig : plt.Figure):
    # plt.tight_layout()

    if type(file) == str:
        file = pathlib.Path(file)
    file.parent.mkdir(parents=True, exist_ok=True)
    fig.savefig(file, format="pdf", dpi=1200, bbox_inches="tight")