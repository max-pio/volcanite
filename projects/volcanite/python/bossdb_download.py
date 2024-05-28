import os

from intern import array
import numpy as np

# ----------- CONFIG --------------
# data set to download (browse bossdb.org for available data)
BOSSDB_DATASET = "witvliet2020/Dataset_8/segmentation"

# chunks of size [CHUNK_SIZE, CHUNK_SIZE, CHUNK_SIZE] are processed one after the other
CHUNK_SIZE = 1024
# at which voxel position to start the download:
start_z = 0
start_y = 6144
start_x = 18432   # (first chunk of C Elegans data set contains some random 1 labels)
# ---------------------------------

# obtain cloud data handle from bossdb
bossdb_dataset = array("bossdb://" + BOSSDB_DATASET)
print("Accessing data set " + str(BOSSDB_DATASET) + " with shape (ZYX) " + str(bossdb_dataset.shape))

full_dim_x = bossdb_dataset.shape[2]
full_dim_y = bossdb_dataset.shape[1]
full_dim_z = bossdb_dataset.shape[0]



min_non_empty_x = 99999999
min_non_empty_y = 99999999
min_non_empty_z = 99999999
max_non_empty_x = 0
max_non_empty_y = 0
max_non_empty_z = 0


os.makedirs("./" + BOSSDB_DATASET + "/", exist_ok=True)
if not os.listdir("./" + BOSSDB_DATASET + "/") == []:
    print("Aborting: directory ./" + BOSSDB_DATASET + "/ must be empty")
    exit(0)

# each step downloads and processes a subvolume of size [CHUNK_SIZE, CHUNK_SIZE, CHUNK_SIZE]
for z in range(start_z, full_dim_z, CHUNK_SIZE):
    for y in range(start_y, full_dim_y, CHUNK_SIZE):
        for x in range(start_x, full_dim_x, CHUNK_SIZE):

            # download next chunk
            x_end = min(full_dim_x, x + CHUNK_SIZE)
            y_end = min(full_dim_y, y + CHUNK_SIZE)
            z_end = min(full_dim_z, z + CHUNK_SIZE)
            cur_slice = bossdb_dataset[z:z_end, y:y_end, x:x_end].astype('uint32')
            max_label = np.max(cur_slice)

            # update progress
            progress = (z_end * full_dim_y * full_dim_x + y_end * full_dim_x + x_end) / (full_dim_x * full_dim_y * full_dim_z)
            print(str(int(progress * 100.0)) + "%   | slice (ZYX) [" + str(z) + ":" + str(z_end) + ", " + str(y) + ":" + str(y_end) + ", " + str(x) + ":" + str(x_end) + "]   | max. label: " + str(max_label))

            # ignore the label 0 everywhere, append coordinates to files for all other labels
            if max_label > 0:
                # keep track of minimum and maximum voxel that contains non-emtpy (> 0) regions
                min_non_empty_x = min(min_non_empty_x, x)
                min_non_empty_y = min(min_non_empty_y, y)
                min_non_empty_z = min(min_non_empty_z, z)
                max_non_empty_x = max(max_non_empty_x, x_end)
                max_non_empty_y = max(max_non_empty_y, y_end)
                max_non_empty_z = max(max_non_empty_z, z_end)

                # output coordinate lists to file (3x uint32 values per coordinate, in ZXY order)
                for l in range(1, max_label + 1):
                    coords = np.argwhere(cur_slice == l)
                    if len(coords) > 0:
                        coords += [z, y, x]
                        coords = coords.flatten().astype('uint32')
                        if cur_slice[coords[0] - z, coords[1] - y, coords[2] - x] != l:
                            print("wrong label, expected " + str(l) + " at " + str(coords[0:3]) + " but got " + str(cur_slice[coords[0] - z, coords[1] - y, coords[2] - x]))

                        # append to file
                        with open("./" + BOSSDB_DATASET + "/" + str(l) + ".idx", "a") as file:
                            coords.tofile(file)

                # write out start coordinates of this non-empty chunk to file
                with open("./" + BOSSDB_DATASET + "/non_empty_xyz_chunks.txt", "a") as file:
                    file.write(str(x) + " " + str(y) + " " + str(z) + "\n")

print("done")
print(" ----- ")
print("Minimum non-empty voxel coordinates (ZYX): " + str([min_non_empty_z, min_non_empty_y, min_non_empty_x]))
print("Maximum non-empty voxel coordinates (ZYX): " + str([max_non_empty_z, max_non_empty_y, max_non_empty_x]))
