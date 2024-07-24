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

import numpy as np
import matplotlib.pyplot as plt
import pymorton as pm

def generate_pixel_sequence_morton(grid_size):
    coords = []
    n = 1
    while n <= grid_size:
        for x in range(n * n):
            morton_coord = pm.deinterleave2(x)
            new_coord = [morton_coord[0] * grid_size // n, morton_coord[1] * grid_size // n]
            if not (new_coord in coords):
                coords.append(new_coord)
        n = n*2
    return np.asarray(coords)

def plot_sequence(seq):
    x = seq[:, 0]
    y = seq[:, 1]
    i = range(seq.shape[0])
    plt.scatter(x, y, c=i, cmap='gray')
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
    seq = generate_pixel_sequence_morton(2)
    print_as_cpp_array(seq)
    #plot_sequence(seq)