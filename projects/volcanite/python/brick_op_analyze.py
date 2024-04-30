import math

import matplotlib.pyplot as plt
import matplotlib.colors as mcolors
import numpy as np


def read_operation_stream(path):
    with open(path, "rb") as file:
        operations = np.fromfile(file, dtype='uint32')
    return operations


def read_brick_starts(path):
    with open(path, "rb") as file:
        operation_starts = np.fromfile(file, dtype='uint32')
        operation_starts = operation_starts.reshape((-1, 2))
    return operation_starts

def plot_operations(brick_id):
    begin = starts[brick_id][0]
    end = starts[brick_id+1][0]
    stream = ops[begin:end].astype('float32')
    detail_count = starts[brick_id][1]

    # mark the base level encodings
    for i in range(detail_count):
        stream[i] = stream[i] - 16

    # the following operations exist:
    # base levels
    #  -16 PARENT, -15 NX, -14 NY, -13 NX, -11 PA, -10 P1
    #  with stop bits set:
    #  -8 PARENT, -15 NX, -14 NY, -13 NX, -11 PA, -10 P1
    # detail level
    #  0 PARENT, 1 NX, 2 NY, 3 NX, 5 PA, 6 P1
    print(np.unique(stream))

    # construct a colormap from [-16, 6]
    base_colors = plt.cm.viridis(np.linspace(0, 1, 15))
    detail_colors = plt.cm.coolwarm(np.linspace(0, 1, 7))
    # combine them and build a new colormap
    colors = np.vstack((base_colors, detail_colors))
    print(colors)
    stacked_cmap = mcolors.LinearSegmentedColormap.from_list('operations', colors)

    # reshape the stream (padded with NaN) to a square display
    next_square = math.ceil(math.sqrt(stream.shape[0]))
    elem_pad = (next_square * next_square) - stream.shape[0]
    stream = np.pad(stream, (0, elem_pad), constant_values=(np.nan, np.nan))
    stream = stream.reshape(next_square, next_square)

    plt.title("Operations of Brick " + str(brick_id))
    plt.imshow(stream, cmap=stacked_cmap)
    plt.show()

if __name__ == '__main__':
    # config
    prefix = "D:/data/cellsinsilico/Big01/000_longer/outdir/nrrd_uint32/cells_frame065_100x100x100.raw_operations"

    # load operation and start stream
    opstrm_postfix = "_op.raw"
    starts_postfix = "_op_starts.raw"
    ops = read_operation_stream(prefix + opstrm_postfix)
    starts = read_brick_starts(prefix + starts_postfix)

    # plot the operations of one
    plot_operations(10)