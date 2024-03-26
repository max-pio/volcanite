import io
import sys

import pyvista as pv
import numpy as np
import h5py
import compresso
import lzma
from timeit import default_timer as timer
import PIL.Image as Image
import matplotlib.pyplot as plt


def read_from_NRRD(path):
    file = open(path[:-4] + ".raw", "rb")
    # write header
    shape_str = file.readline()[:-1].decode('ascii').split()
    type = file.readline()[:-1].decode('ascii')
    # write binary
    volume = np.fromfile(file, dtype=type)
    volume = volume.reshape([int(shape_str[0]), int(shape_str[1]), int(shape_str[2])])
    file.close()
    return volume

def read_from_hdf5(path):
    f = h5py.File(path, 'r')
    print("hdf5 file loaded with data shape " + str(f["data"].shape) + " and type " + str(f["data"].dtype))
    return np.asarray(f["data"])

if __name__ == '__main__':

    path_sg = "/home/maxpio/data/segmented_volumes/mouse_cortex/mapped/chunks/x2y3z2.hdf5"
    path_gs = "/home/maxpio/data/segmented_volumes/mouse_cortex/mapped/grayscale/x2y3z2.hdf5"

    sg = read_from_hdf5("/home/maxpio/data/segmented_volumes/mouse_cortex/mapped/chunks/x2y3z2.hdf5")
    gs = read_from_hdf5("/home/maxpio/data/segmented_volumes/mouse_cortex/mapped/grayscale/x2y3z2.hdf5")

    slice = 128

    fig = plt.figure()
    ax = plt.subplot(1, 2, 1)
    ax.imshow((sg[:][:][128] * 17) % 256, cmap='turbo')
    ax = plt.subplot(1, 2, 2)
    ax.imshow(gs[:][:][128], cmap='gray')


    plt.show()

