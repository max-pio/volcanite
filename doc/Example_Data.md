# Example Data Sets

This is a collection of data sets that are available and can be rendered in Volcanite.

### Dense Connectomic Reconstruction in Layer 4 of the Somatosensory Cortex

[Download](https://l4dense2019.brain.mpg.de/webdav/segmentation-volume/) | [Repository](https://l4dense2019.brain.mpg.de/) | [Paper](https://science.sciencemag.org/lookup/doi/10.1126/science.aay3134)

**Description:** A segmentation of a scan of a mouse cortex where neurons and axons are labeled.
The data is extremely large, and split in 216 chunks of 1024³ voxels.

| Voxels             | Labels    | Format          | Uncompressed  | Chunked                |
|--------------------|-----------|-----------------|---------------|------------------------|
| 6144 x 9216 x 4096 | 5,030,572 | Compressed hdf5 | 927.7GB       | 6 x 9 x 3 1024³ chunks |

Note: The Volcanite backend offers support for compressing chunked data and is able to render the full data set interactively.
However, for your fist contact with the framework it is recommended to only process a single chunk.

*Motta A, Berning M, Boergens KM, Staffler B, Beining M, Loomba S, Hennig Ph, Wissler H, Helmstaedter M (2019). Dense connectomic reconstruction in layer 4 of the somatosensory cortex. Science. DOI: 10.1126/science.aay3134* 