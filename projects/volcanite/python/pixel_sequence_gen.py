#  Copyright (C) 2024, Max Piochowiak, Karlsruhe Institute of Technology
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
import math

import numpy as np
import matplotlib as mpl
import matplotlib.pyplot as plt
import pymorton as pm
from matplotlib import animation


def reverse_bits(x, bitcount):
    bits = '{:0{bc}b}'.format(x, bc=bitcount)
    return int(bits[::-1], 2)

def generate_bitfield_reverse_morton_pixel_sequence(grid_size):
    bits_per_coord = int(math.log2(grid_size * grid_size))

    coords = []
    for x in range(grid_size * grid_size):
        coords.append(pm.deinterleave2(reverse_bits(x, bits_per_coord)))
    return np.asarray(coords)

def plot_sequence(seq):
    x = seq[:, 0]
    y = seq[:, 1]
    i = range(seq.shape[0])
    for j in range(seq.shape[0] - 1):
        plt.plot([x[j], x[j+1]], [y[j], y[j+1]], c=mpl.colormaps['viridis'](j / seq.shape[0]))
    plt.scatter(x, y, s=100, c=i, cmap='viridis')
    plt.show()

def plot_sequence_anim(seq, grid_size):
    x = seq[:, 0]
    y = seq[:, 1]
    indices = list(range(seq.shape[0]))

    fig, ax = plt.subplots()
    scat = ax.scatter(x[0], y[0], c=indices[0], s=100, cmap='viridis')
    ax.set(xlim=[0, grid_size], ylim=[0, grid_size], xlabel='X', ylabel='Y')
    def update(frame):
        # update the scatter plot:
        data = np.stack([x[:frame], y[:frame]]).T
        scat.set_offsets(data)
        scat.set_array(indices[:frame])
        return scat

    ani = animation.FuncAnimation(fig=fig, func=update, frames=grid_size * grid_size, interval=80)
    plt.show()

def print_as_cpp_array(seq):
    print("{", end="")
    for i in range(seq.shape[0]):
        print("{" + str(seq[i, 0]) + "," + str(seq[i, 1]) + "}", end="")
        if i % 4 != 3:
            print(", ", end="")
        elif i < (seq.shape[0] - 1):
            print(",")
    print("};")


if __name__ == '__main__':
    seq = generate_bitfield_reverse_morton_pixel_sequence(2)

    print_as_cpp_array(seq)
    # plot_sequence(seq)
    # plot_sequence_anim(seq, 8)