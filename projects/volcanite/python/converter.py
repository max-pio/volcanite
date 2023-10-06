import io
import sys

import pyvista as pv
import numpy as np
import h5py
import compresso
import lzma
from timeit import default_timer as timer
import PIL.Image as Image

def vtk_cells_to_numpy(volume):
    return volume.active_scalars.reshape(np.array(volume.dimensiopns) - 1)

def export_vtk_to_hdf5(path):
    vtk = pv.read(path)
    with h5py.File(path[:-4] + ".hdf5", "w") as f:
        dset = f.create_dataset("data", shape=(np.array(vtk.dimensions) - 1), dtype='uint32', data=vtk.active_scalars,  compression="gzip")
    read_from_hdf5(path[:-4] + ".hdf5")

def read_from_hdf5(path):
    f = h5py.File(path, 'r')
    print("hdf5 file loaded with data shape " + str(f["data"].shape) + " and type " + str(f["data"].dtype))
    return np.asarray(f["data"])

def export_vtk_to_NRRD(path, out_type="uint32"):
    volume = vtk_cells_to_numpy(pv.read(path))
    file = open(path[:-4] + ".raw", "wb")
    # write header
    file.write((str(volume.shape[0]) + " " + str(volume.shape[1]) + " " + str(volume.shape[2]) + "\n").encode('utf8'))
    file.write((out_type + "\n").encode('utf8'))
    # write binary
    volume.astype(out_type).tofile(file)
    file.close()

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


def enc_hdf5(labels):
    # this exports the output to a temp .hdf5 file
    with h5py.File("./tmp_hdf5_enc_export.hdf5", "w") as f:
        start = timer()
        dset = f.create_dataset("data", shape=labels.shape, dtype='uint32',
                                data=labels.flatten(), compression="gzip", chunks=(128, 128, 128))
        end = timer()
        bytes = dset.id.get_storage_size()
        print("hdf5           Compression rate: " + str(bytes / (labels.shape[0] * labels.shape[1] * labels.shape[2] * 4)) + " in " + str(end - start) + "s")
    # remove temp file
    os.remove("./tmp_hdf5_enc_export.hdf5")

def enc_compresso(labels, use_lzma):
    start = timer()
    compressed_labels = compresso.compress(labels, steps=(8,8,1))  # 3d numpy array -> compressed bytes
    if use_lzma:
        compressed_labels = lzma.compress(compressed_labels)#, preset=(9|lzma.PRESET_EXTREME))
    end = timer()
    if use_lzma:
        print("Compresso LZMA Compression rate: " + str(len(compressed_labels)/(labels.shape[0] * labels.shape[1] * labels.shape[2] * 4)) + " in " + str(end - start) + "s")
    else:
        print("Compresso      Compression rate: " + str(
            len(compressed_labels) / (labels.shape[0] * labels.shape[1] * labels.shape[2] * 4)) + " in " + str(
            end - start) + "s")

def enc_neuroglancer(labels):
    print("compress")
    start = timer()
    compressed_labels = neuroglancer.neuroglancer.compress(labels)  # 3d numpy array -> compressed bytes
    end = timer()
    print("Compression rate: " + str(
        len(compressed_labels) / (labels.shape[0] * labels.shape[1] * labels.shape[2] * 4)) + " in " + str(end - start) + "s")
    print("Decompression: in " + str(end - start) + "s")


def enc_slice_png(labels):
    total_bytes = 0
    total_time = 0
    for z in range(labels.shape[0]):
        slice = np.stack([labels[z] % 256, (labels[z]/256)%256, (labels[z]/(256*256))%256, (labels[z]/(256*256*256)%256)], axis=-1)
        image = Image.fromarray(slice.astype('uint8'))
        b = io.BytesIO()
        start = timer()
        image.save(b, 'png', compress_level=9)
        end = timer()
        total_time += end-start
        total_bytes += b.getbuffer().nbytes
        b.close()
    # image.save("./tmp_png_out.png")
    print("PNG            Compression rate: " + str(total_bytes / (labels.shape[0] * labels.shape[1] * labels.shape[2] * 4)) + " in " + str(total_time) + "s")



if __name__ == '__main__':

    path = ""
    if len(sys.argv) > 1:
        path = sys.argv[1]
    else:
        print("provide a path to a data set")
        exit(1)

    # export segmentation volume to hdf5 file: -------------------------------------------------------------------------
    # export_vtk_to_hdf5(path)
    # export_vtk_to_nrrd(path)
    # exit(0)

    # encode segmentation volume internally and output compression rates: ----------------------------------------------
    if path[-1] == "w":     # simplified nrrd (.raw)
        labels = read_from_NRRD(path)
    elif path[-1] == "5":   # hdf5 (.hdf5)
        labels = read_from_hdf5(path)
    elif path[-1] == "i":   # VTK image file (.vti)
        labels = vtk_cells_to_numpy(pv.read(path))
    elif path[-1] == "p":   # numpy raw array (.np)
        if len(sys.argv) < 5:
            print("X Y Z volume dimensions must be provided for raw numpy file imports as command line arguments")
            exit(1)
        DIM_X = int(sys.argv[2])
        DIM_Y = int(sys.argv[3])
        DIM_Z = int(sys.argv[4])
        labels = np.fromfile("numpy_file.np", dtype='uint32', count=DIM_X*DIM_Y*DIM_Z)
        labels.reshape((DIM_X, DIM_Y, DIM_Z))
    else:
        print("can't recognize filetype")
        exit(1)

    # enc_slice_png(labels)
    # enc_compresso(labels, False)
    # enc_compresso(labels, True)
    # enc_neuroglancer(labels)
    # exit(0)
