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

import matplotlib.pyplot as plt
from converter import *

if __name__ == '__main__':

    # config
    path_sg = "./download/example_segmentation.hdf5"
    path_gs = "./download/example_grayscale.hdf5"

    # display segmentation next to grayscale volume
    sg = read_hdf5(path_sg)
    gs = read_hdf5(path_gs)

    volume_slice = sg.shape[2] // 2

    fig = plt.figure()
    ax = plt.subplot(1, 2, 1)
    ax.imshow((sg[:][:][volume_slice] * 17) % 256, cmap='turbo')
    ax = plt.subplot(1, 2, 2)
    ax.imshow(gs[:][:][volume_slice], cmap='gray')
    plt.show()
