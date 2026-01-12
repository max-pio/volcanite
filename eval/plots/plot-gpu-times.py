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
            fig = plot_gpu_timings(df, stages=["Cache","Decompress","Render","Post-Process"], frames=21, xlabel="Image Frame")
            save_plot(f"../results/image-eval/{data}_{shading}_timing.pdf", fig)
