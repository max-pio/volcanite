import io
import sys

import pyvista as pv
import numpy as np
import h5py
import compresso
import lzma
from timeit import default_timer as timer
import PIL.Image as Image
import neuroglancer
import tempfile
#from tqdm import tqdm

def vtk_cells_to_numpy(volume):
    return volume.active_scalars.reshape(np.array(volume.dimensiopns) - 1)

def export_to_hdf5(path):
    vtk = pv.read(path)
    with h5py.File(path[:-4] + ".hdf5", "w") as f:
        dset = f.create_dataset("data", shape=(np.array(vtk.dimensions) - 1), dtype='uint32', data=vtk.active_scalars,  compression="gzip")
    read_from_hdf5(path[:-4] + ".hdf5")

def read_from_hdf5(path):
    f = h5py.File(path, 'r')
    print("hdf5 file loaded with data shape " + str(f["data"].shape) + " and type " + str(f["data"].dtype))
    return np.asarray(f["data"])

def export_to_NRRD(path, out_type="uint32"):
    volume = vtk_cells_to_numpy(pv.read(path))
    file = open(path[:-4] + ".raw", "wb")
    # write header
    file.write((str(volume.shape[0]) + " " + str(volume.shape[1]) + " " + str(volume.shape[2]) + "\n").encode('utf8'))
    file.write((out_type + "\n").encode('utf8'))
    # write binary
    volume.astype(out_type).tofile(file)
    file.close()

def export_numpy_to_hdf5(labels, path):
    with h5py.File(path[:-4] + ".hdf5", "w") as f:
        start = timer()
        dset = f.create_dataset("data", shape=labels.shape, dtype='uint32',
                                data=labels.flatten(), compression="gzip", chunks=(128, 128, 128))
        end = timer()
        bytes = dset.id.get_storage_size()
        print("hdf5           Compression rate: " + str(bytes / (labels.shape[0] * labels.shape[1] * labels.shape[2] * 4)) + " in " + str(end - start) + "s")


def read_from_NRRD(path, in_type="uint32"):
    file = open(path[:-4] + ".raw", "rb")
    # write header
    shape_str = file.readline()[:-1].decode('ascii').split()
    type = file.readline()[:-1].decode('ascii')
    # write binary
    volume = np.fromfile(file, dtype=type)
    volume = volume.reshape([int(shape_str[0]), int(shape_str[1]), int(shape_str[2])])
    file.close()
    return volume

def compress_difftree_lzma(path):
    file = open(path, 'rb')
    b = bytearray(file.read())
    original_size = 1000 * 1000 * 1000 * 4
    print("Simple encoding: " + str(len(b) / original_size))
    start=timer()
    b2 = lzma.compress(b)
    end=timer()
    print("LZMA: " + str(len(b2) / original_size) + " in " + str(end - start) + "s")
    file.close()


def enc_compresso(labels, use_lzma):
    start = timer()
    compressed_labels = compresso.compress(labels, steps=(8,8,1))  # 3d numpy array -> compressed bytes
    if use_lzma:
        compressed_labels = lzma.compress(compressed_labels) #, preset=(9|lzma.PRESET_EXTREME))
    end = timer()
    if use_lzma:
        print("Compresso LZMA Compression rate: " + str(len(compressed_labels)/(labels.shape[0] * labels.shape[1] * labels.shape[2] * 4)) + " in " + str(end - start) + "s")
    else:
        print("Compresso      Compression rate: " + str(
            len(compressed_labels) / (labels.shape[0] * labels.shape[1] * labels.shape[2] * 4)) + " in " + str(
            end - start) + "s")
    # print("decompress")
    # start = timer()
    # if use_lzma:
    #     reconstituted_labels = compresso.decompress(lzma.decompress(compressed_labels))  # compressed bytes -> 3d numpy array
    # else:
    #     reconstituted_labels = compresso.decompress(compressed_labels)
    # end = timer()
    # print("Decompression: in " + str(end - start) + "s")
    # print("Test okay: " + str(np.all(np.equal(labels, reconstituted_labels))))

def enc_compresso_chunks(zwidth, lzma):
    labels = read_from_hdf5("/home/maxpio/data/segmented_volumes/fiber_polymer/a/glassfibrereinforcedpolymer_unloaded_1579x1092x1651_2umVS_labeled_16bit.hdf5")
    with open("/home/maxpio/data/segmented_volumes/compresso.tmp", mode="wb") as bytefile:
        for z in range(0, labels.shape[2], zwidth):
            end_z = min((z+1) * zwidth, labels.shape[2])
            a = labels[:, :, (z*zwidth):end_z]

            start = timer()
            compresso_out = compresso.compress(a, steps=(8, 8, 1))  # 3d numpy array -> compressed bytes
            end = timer()
            print("byte size: " + str(len(compresso_out) - 36))
            print("header: " + str(compresso.raw_header(compresso_out)))
            print("header? " + str(compresso_out[:36]))
            bytefile.write(compresso_out[36:])


def enc_neuroglancer(labels):
    start = timer()
    compressed_labels = neuroglancer.compress(labels)  # 3d numpy array -> compressed bytes
    end = timer()
    print("neuroglancer   Compression rate: "
          + str(len(compressed_labels) / (labels.shape[0] * labels.shape[1] * labels.shape[2] * 4))
          + " in " + str(end - start) + "s")


def enc_slice_png(labels):
    total_bytes = 0
    total_time = 0
    # for z in tqdm(range(labels.shape[0])):
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
    # image.save("/home/max/tmp_png_out.png")
    print("PNG            Compression rate: " + str(total_bytes / (labels.shape[0] * labels.shape[1] * labels.shape[2] * 4)) + " in " + str(total_time) + "s")



if __name__ == '__main__':

    path = "/home/maxpio/data/cellsinsilico/Big01/000/outdir/nrrd_uint32/cells_frame055.raw"
    #path = "/home/maxpio/data/segmented_volumes/fiber_polymer/a/glassfibrereinforcedpolymer_unloaded_1579x1092x1651_2umVS_labeled_16bit.hdf5"

    if len(sys.argv) > 1:
        path = sys.argv[1]

    if path[-1] == "w":
        labels = read_from_NRRD(path)
    elif path[-1] == "5":
        labels = read_from_hdf5(path)
    else:
        print("unrecognized filetyp")
        exit(1)

    # HDF5------------------
    with tempfile.TemporaryDirectory() as tmpdir:
        export_numpy_to_hdf5(labels, tmpdir + "/hdf5_comp.hdf5")

    ## Neuroglancer--------- (use C++ version)
    # enc_neuroglancer(labels)

    ### PNG------------------
    enc_slice_png(labels)

    ## Compresso------------- (Use C++ version?)
    enc_compresso(labels, False)
    enc_compresso(labels, True)

    exit(0)

# 