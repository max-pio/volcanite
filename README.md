# Volcanite Segmentation Volume Renderer
Volcanite is a GPU renderer for segmentation volumes implemented using C++ and the Vulkan API.
Segmentation volumes are voxel data sets that store an integer object label per voxel.
These volumes are commonly used in fields like connectomics, material science, computational biology, medicine, among others.
Volcanite uses a state-of-the-art [segmentation volume compression](https://cg.ivd.kit.edu/english/compsegvol.php) to
store large volume data sets in GPU memory during rendering.
Apart from interactively or non-interactively visualizing segmentation volumes, Volcanite can be used to compress data
sets at compression rates that generally outperforming other methods.

![Renderer Preview Image](doc/volcanite_app.jpg)

## Quick Start
See the setup guides for [Ubuntu / Debian](doc/Setup.md#ubuntu--debian) or [Windows](doc/Setup.md#ubuntu--debian) respectively for a more detailed description on how to install all dependencies and build Volcanite.
To install all required and optional dependencies under Ubuntu, use
```
sudo apt install build-essential cmake libglfw3-dev libhdf5-dev libvtk9-dev libtiff-dev libpugixml-dev libsqlite3-dev -y
```
and build the `volcanite` executable with
```
mkdir cmake-build-release && cd cmake-build-release
cmake -DCMAKE_BUILD_TYPE=Release .. && cmake --build . --target volcanite
```
Start Volcanite, either providing a path to a segmentation volume as a commandline argument with
```
./projects/volcanite/volcanite /path/to/your/segmentation/volume
```
or by using the file dialog to select a volume file.
Run `./volcanite --help` for a complete list of arguments and commands.
See [Usage.md](doc/Usage.md#supported-segmentation-volume-file-formats) for a list of currently supported formats.
You can find a collection of example data sets listed in [ExampleData.md](doc/ExampleData.md).

## Documentation
The `doc` directory of this repository contains further information on how to use Volcanite:
* [Setup.md](doc/Setup.md) provides the setup guides for Linux and Windows systems and headless builds.
* [Usage.md](doc/Usage.md) contains the guide for using the Volcanite GUI application or command line interface.
* [ExampleData.md](doc/ExampleData.md) lists segmentation volumes that are publicly available and how to access them.
* [Python.md](doc/Python.md) contains information on how to export data sets from python to be processed with Volcanite. 
* [Development.md](doc/Development.md) explains some rendering and compression algorithms and contains development guides.

## License
If not stated otherwise, the code in this repository uses a GPLv3 license.
If you require alternative licensing options, please contact the authors.

## About
This renderer was created at by [Max Piochowiak](https://cg.ivd.kit.edu/piochowiak/staff_index.php) with significant code
contributions by [Reiner Dolp](https://cg.ivd.kit.edu/english/staff_2590.php). Both are affiliated with Karlsruhe Institute of Technology (KIT).
You can cite the following publication if you use Volcanite in your projects:

```bibtex
@article{Piochowiak:2024:csgv,
    author={Piochowiak, Max and Dachsbacher, Carsten},
    journal={IEEE Transactions on Visualization and Computer Graphics}, 
    title={Fast Compressed Segmentation Volumes for Scientific Visualization}, 
    year={2024},
    volume={30},
    number={1},
    pages={12-22},
    doi={10.1109/TVCG.2023.3326573}
}
```

### Funding
This work has been supported by the Helmholtz Association (HGF) under the joint research school
“HIDSS4Health – Helmholtz Information and Data Science School for Health” and through the Pilot Program
Core Informatics.
