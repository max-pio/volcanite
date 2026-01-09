import pandas as pd
import matplotlib.pyplot as plt
from matplotlib import rc, colormaps
from matplotlib.colors import ListedColormap
from cycler import cycler


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
colormaps.register(volcanite_cmap, name='volcanite_cmap')  # For Matplotlib 3.7+ [web:3]

def init_plots():
    rc("text", usetex=True)
    rc("font", family="serif")
    # rc("image", cmap="volcanite_cmap")
    rc("axes", edgecolor='black')
    plt.rc('axes', prop_cycle=(cycler(color=volcanite_colors, edgecolor=volcanite_colors_dark)))