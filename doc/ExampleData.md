# Example Data Sets

This is a collection of segmentation volume data sets that are freely available and can be rendered in Volcanite.
For file formats that are not supported by Volcanite, see the [converter.py](../volcanite/python/converter.py) python script for converting
formats.
To download tensorstore volumes or publicly available data sets from BossDB, have a look at the
[download_cloud_data.py](../volcanite/python/download_cloud_data.py) python scripts.

## Selected Segmentation Volumes

### Dense Connectomic Reconstruction in Layer 4 of the Somatosensory Cortex

[Download](https://l4dense2019.brain.mpg.de/webdav/segmentation-volume/) | [Repository](https://l4dense2019.brain.mpg.de/) | [Paper](https://science.sciencemag.org/lookup/doi/10.1126/science.aay3134)

**Description:** A segmentation of a scan of a mouse cortex where neurons and axons are labeled.
The data is extremely large, and split in 216 chunks of 1024³ voxels.

| Voxels             | Labels    | Format          | Uncompressed     | Chunked                |
|--------------------|-----------|-----------------|------------------|------------------------|
| 6144 x 9216 x 4096 | 5,030,572 | Compressed hdf5 | 927.7GB [uint32] | 6 x 9 x 3 1024³ chunks |

Note: The Volcanite backend offers support for compressing chunked data and is able to render the full data set interactively.
However, for your fist contact with the framework it is recommended to only process a single chunk.

*Motta A, Berning M, Boergens KM, Staffler B, Beining M, Loomba S, Hennig Ph, Wissler H, Helmstaedter M (2019). Dense connectomic reconstruction in layer 4 of the somatosensory cortex. Science. DOI: 10.1126/science.aay3134* 


### H01 - A Browsable Petascale Reconstruction of the Human Cortex

[Download](https://l4dense2019.brain.mpg.de/webdav/segmentation-volume/) | [Website](https://h01-release.storage.googleapis.com/landing.html) | [Paper](https://www.science.org/doi/10.1126/science.adk4858)

**Description:** A petabyte volume of a small sample of human brain tissue. See the accompanying online [jupyter notebook](https://colab.research.google.com/gist/jbms/1ec1192c34ec816c2c517a3b51a8ed6c/h01_data_access.ipynb#scrollTo=rtimT0EkY93k) for the Python download interface.

| Voxels                 | Labels | Format                   | Uncompressed       | Chunked     |
|------------------------|--------|--------------------------|--------------------|-------------|
| 515892 x 356400 x 5293 | ?      | neuroglancer precomputed | 3540.45TB [uint32] | tensorstore |

Note: As Volcanite relies on storing the full compressed volume in main memory - and a significant potion of it in GPU memory -
the petascale data set exceeds its capabilities. You can however visualize sub-volumes of up to 8000³ voxels, depending on your system. 

*Shapson-Coe A, et al. (2024) A petavoxel fragment of human cerebral cortex reconstructed at nanoscale resolution. Science DOI: 10.1126/science.adk4858*

## Online Segmentation Volume Collections

### [BossDB](https://bossdb.org/)
Browse the [data sets](https://bossdb.org/projects) and filter for `volumetric segmentation`. Data sets can be downloaded [via Python](https://bossdb.org/get-started):
```Python
from intern import array
# Save a cutout to a numpy array in ZYX order:
bossdb_dataset = array("bossdb://kuan_phelps2020/drosophila_brain_120nm/drBrain_120nm_rec")
print(bossdb_dataset.shape)
my_cutout = bossdb_dataset[0:720, 1000:2024, 1000:2024]
```
### [DRYAD](https://datadryad.org/stash)
### [Papers With Code](https://paperswithcode.com/datasets)
### [OSF](https://osf.io/)
### [Google Connectomics Group](https://research.google/teams/connectomics/)
### [Neurodata](https://neurodata.io/project/ocp/)
### [Zenodo](https://zenodo.org/)
### [OpenMicroscopy](https://idr.openmicroscopy.org/)