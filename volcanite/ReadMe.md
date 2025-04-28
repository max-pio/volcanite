# Volcanite Segmentation Volume Renderer

> Vulcanite: a type of rock that is commonly used as heating elements for sauna stoves.

# Rendering Pipeline

The main executable target for the renderer is [volcanite.cpp](./src/bin/volcanite.cpp).
The renderer has a host (CPU) and device (GPU) site.
The host side uses the PassCompute abstraction from *libvvv* and is implemented in
the [CompressedSegmentationVolumeRenderer](./include/volcanite/renderer/CompressedSegmentationVolumeRenderer.hpp)
and [PassCompSegVolRender](./include/volcanite/renderer/PassCompSegVolRender.hpp) classes.
On device site, everything is implemented in GLSL compute shaders which you can find
in [data/shader/volcanite/renderer](./data/shader/volcanite/renderer).
Most of the shader stages are concerned with the brick cache management and decompression.
To get an idea of the inner workings of those stages, have a look at the supplemental material of
the ["Shading Atlas Streaming"](https://www.tugraz.at/institute/icg/research/team-steinberger/research-projects/sas)
paper since our cache management follows its fundamental structure.

```
                                                                                                              ┌──────────────┐
┌─────────────┐ ┌───────────────┐ ┌─────────────────┐ ┌──────────────┐ ┌────────────┐ ┌────────────────────┐ ┌▼────────────┐ │
│ Cache Reset ├─► Cache Request ├─► Cache Provision ├─► Cache Assign ├─► Decompress ├─► Ray March Renderer ├─► Post-Process├─┘
└─────────────┘ └───────────────┘ └─────────────────┘ └──────────────┘ └────────────┘ └────────────────────┘ └─────────────┘  
──────v──────── ────────────────────────v───────────────────────────── ──────v─────── ──────────v─────────── ───────v───────  
   Optional     Brick Cache Stages, similar to Shading Atlas Streaming   new Bricks   0-1 Ray Paths / Pixel  Denoise Upsample 
```

You can find the main renderer in [csgv_renderer.comp](./data/shader/volcanite/renderer/csgv_renderer.comp).
The caching and decompression for visible volume bricks is handled in
the [csgv_request](./data/shader/volcanite/renderer/csgv_request.comp), [csgv_resolve](./data/shader/volcanite/renderer/csgv_resolve.comp),
and [csgv_assign](./data/shader/volcanite/renderer/csgv_assign.comp) shaders.
As the renderer uses the brick compression from the
paper ["Fast Compressed Segmenation Volumes for Scientific Visualization"](https://cg.ivd.kit.edu/english/compsegvol.php),
additional shaders for accessing compressed segmentation volumes (CSGV) and the different brick decoders are located in [data/shader/compression](data/shader/volcanite/compression).

**Ray Marching Initialization:**
Rendering happens with one thread per pixel.
A view ray is sent out for which entry and exit depth for an axis aligned bounding box of the volume is computed.
In world space, this box is centered around the origin and normalized so that its largest dimension is 1.
However, we compute voxel marching in model space where each voxel is a unit cube and the smallest coordinate is (
0,0,0).
This allows for a faster and more straightforward implementation of [digital differential analyzer (DDA) voxel traversal](http://www.cse.yorku.ca/~amana/research/grid.pdf).

**Ray Marching Traversal:**
Depending on the rendering configuration, indirect shadow, ambient occlusion, or path tracing rays are evaluated after the first surface hit point.
The same traversal loop handles all ray types to reduce thread divergence.
To that end, the ray has a type - or state - depending on if it is a primary camera view ray or a secondary ray into
which it is transformed after a hit.
We use a multi-level DDA traversal with a custom numeric decomposition to prevent aliasing from sampling voxels even at large voluem dimensions.

**Handling of Ray Hits:**
In the renderer, the segmentation voxel data sets are (mostly) opaquely handled as bricks with a power of two brick size
in a certain level-of-detail (LOD) while each voxel stores one unsigned integer as its label.
If the ray marching (`Ray March Renderer`) acesses a brick that is not in the cache yet, it is requested and will be decoded in the next frame.
All requests are for a certain level-of-detail which only depends on the distance between brick center and camera.
This way, all rays request the same LOD and no race conditions occur in the request buffer.
If a ray hits an undecoded brick, its coarsest representation (the brick's first palette entry) is read from the volume
encoding instead of a cache value.
Colors of such

**Temporal Accumulation:**
Ping-pong buffers are used for temporal accumulation of RGB radiance and sample counts.
After rendering obtained new pixel samples, a post-processing pipeline is carried out to fill non-rendered pixels trough upsampling and remove Monte Carlo noise of ambient occlusion or path tracing through denoising.
The post-processing pipeline is a repeated execution of the [csgv_denoise_resolve](./data/shader/volcanite/renderer/csgv_denoise_resolve.comp) shader which operates on its own combined denoising and upsampling ping-pong buffer.
The final iteration handles per-pixel operations like color space conversions and blitting to the output color buffer.
In general, our temporal accumulation allows us to

* draw a different number of samples per pixel (including 0) between different pixels in the same frame,
* progressively accumulate path traced lighting or ambient occlusion over time,
* perform temporal anti-aliasing, and
* perform denoised Monte-Carlo path tracing

## Compressed Segmentation Volume Encoding

The different operation-based Compressed Segmentation Volume (CSGV) CPU encoders and decoders are implemented in
[volcanite/compression/encoder/*](./include/volcanite/compression/encoder).
Corresponding GPU decoders can be found in the [decoder](./data/shader/volcanite/compression/decoder) shader directory.
All encoders must have the following properties:

* the compression operates brick-wise,
* bricks have a power-of-two brick size, the encoder may restrict the usage of certain sizes,
* bricks can be (en|de)coded in $log_2(bricksize)+1$ levels-of-detail where each LOD is half the size of the next finer
  LOD,
* the brick encoding buffer ends with a palette of all `uint32` labels occurring in this brick, possibly with duplicates,
* one constant uint index in the brick encoding buffer contains the size of this label palette

The encoders may support different functionality, with optional methods in braces ():

* quick verification for certain invariants of a brick's encoding buffer
* (exporting statistic of bricks and their encoding)
* (decoding with debug information, i.e. the list of CSGV operations)
* (computing operation frequency tables for variable bit-length encoding)
* (detail level separation in the encoding stream)
* (random access decoding of single voxels without decoding full bricks)

The CPU encoders specify a set of compile time defines for the shader compiler.
This includes specialized configuration for a certain encoder as well as a set of common parameters:

* the encoding mode identifier
* the brick size
* the resulting number of levels-of-detail
* the (constant) uint index of a brick's encoding stat stores the palette size

## Relevant Publications

* [Fast Compressed Segmentation Volumes for Scientific Visualization](https://cg.ivd.kit.edu/english/compsegvol.php),
  Piochowiak and Dachsbacher 2023. Transactions on Visualization and Computer Graphics (Proc. IEEE Vis)
* [Shading Atlas Streaming](https://www.tugraz.at/institute/icg/research/team-steinberger/research-projects/sas),
  Mueller et al. 2018. Transactions on Graphics (Proc. SIGASIA)
* [A Fast Voxel Traversal Algorithm for Ray Tracing](http://www.cse.yorku.ca/~amana/research/grid.pdf), Amanatides and
  Woo 1987, (Proc. EG)

<!-- * [A Survey of Temporal Antialiasing Techniques](http://behindthepixels.io/assets/files/TemporalAA.pdf), Yang et al. 2020. Computer Graphics Forum (Proc. EG)
-->
