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

from timingplots import plot_gpu_timings
from common import data_set_ids, shading_mode_ids, save_plot

import pandas as pd
from pathlib import Path


# image rendering (no camera movement)
for data in data_set_ids:
    for shading in shading_mode_ids:

        tag = data + "_" + shading
        gpu_csv = Path("../results/image-eval/" + tag + "_timing.csv")

        if gpu_csv.exists():
            print(f"Plotting {data} {shading}")

            df = pd.read_csv(gpu_csv, comment="#")
            fig, ax = plot_gpu_timings(df, stages=["Cache","Decompress","Render","Post-Process"], frames=21, xlabel="Image Frame")
            save_plot(f"../results/image-eval/{data}_{shading}_timing.pdf", fig)
