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

from timingplots import plot_gpu_timings
from common import init_plots, data_set_ids, shading_mode_ids, save_plot

import pandas as pd
from pathlib import Path
import matplotlib.pyplot as plt
from matplotlib.patches import Patch


# image rendering (no camera movement)
print("--------------\nPlotting Post-Processing Timings")
init_plots()


#for data in data_set_ids:
for data in ["cells"]:
    resolve_csv = Path(f"../results/resolve-video-eval_{data}.csv")

    if resolve_csv.exists():
        print(f"  Plotting post-process timings for {data}")

        plt.figure()
        fig, ax = plt.subplots(constrained_layout=True, figsize=(4, 4))

        df = pd.read_csv(resolve_csv, comment="#")
        stages = ["cache avg [ms]", "decompress avg [ms]", "render avg [ms]", "post-process avg [ms]"]

        postprocess_modes = ["n-1", "n-1/4", "n-1/16", "y-1", "y-1/4", "y-1/16"]
        xlabels = [r"$\frac{1}{1}$",
                   r"$\frac{1}{4}$",
                   r"$\frac{1}{16}$",
                   r"\fbox{$\frac{1}{1}$}",
                   r"\fbox{$\frac{1}{4}$}",
                   r"\fbox{$\frac{1}{16}$}"]


        plot_gpu_timings(df.set_index("Denoising-Subsampling").reindex(postprocess_modes).reset_index()[stages],
                         stages=stages, x=np.arange(6), ylabel=r"Average Frame Time [ms]",
                         xticklabels=xlabels, fig=fig, ax=ax, legend=False)
        save_plot(f"../results/plts/postprocess-timings_{data}.pdf", fig)

        # store for separated legend plot
        handles, _ = ax.get_legend_handles_labels()

        ax.clear()
        plt.clf()
        plt.close('all')

        # Create new figure with ONLY the legend
        legend_path = Path(f"../results/plts/postprocess-timings-legend.pdf")
        if not legend_path.exists():
            print(f"  Plotting post-process timings legend")
            fig_legend = plt.figure(constrained_layout=True)  # Adjust size as needed

            # Create custom patch: white fill, black edge
            custom_patch = Patch(facecolor='white', edgecolor='black', label='Denoise On')
            legend = fig_legend.legend(handles + [custom_patch],
                                       ["Cache", "Decompress", "Render", "Post-Process", "Denoise On"],
                                       loc='center', frameon=True, ncols=5,
                                       handlelength=1.0,
                                       handleheight=1.0,
                                       handletextpad=0.2,
                                       columnspacing=0.4
                                       )

            save_plot(legend_path, fig_legend)
            plt.close(fig_legend)

    else:
        print(f"  Skip post-process timings for {data} (no .csv).")
