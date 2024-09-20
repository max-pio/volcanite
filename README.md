# Volcanite Segmentation Volume Renderer
[![version](https://img.shields.io/badge/version-0.4.0-blue)](https://gitlab.kit.edu/max.piochowiak/volcanite/-/tags/0.4.0)
[![License: GPL v3](https://img.shields.io/badge/License-GPLv3-blue.svg)](https://www.gnu.org/licenses/gpl-3.0)
[![doi](https://img.shields.io/badge/doi-10.1109/TVCG.2023.3326573-blue?logo=ieee&logoColor=white)](https://www.doi.org/10.1109/TVCG.2023.3326573)

Volcanite is a GPU renderer for segmentation volumes implemented using C++ and the Vulkan API.
Segmentation volumes are voxel data sets that store an integer object label per voxel.
These volumes are commonly used in fields like connectomics, material science, computational biology, medicine, among others.
Volcanite uses a state-of-the-art [segmentation volume compression](https://cg.ivd.kit.edu/english/compsegvol.php) to
store large volume data sets in GPU memory during rendering.
Apart from interactively or non-interactively visualizing segmentation volumes, Volcanite can be used to compress data
sets at compression rates that generally outperforming other methods.

![Renderer Preview Image](doc/volcanite_app.jpg)
<sup>Data Set from *Emerging Tumor Development by Simulating Single-cell Events*, Rosenbauer J., Berghoff M., Schug A. (2020) bioRxiv</sup>

## Quick Start
See the setup guides for [Ubuntu / Debian](doc/Setup.md#ubuntu--debian) or [Windows](doc/Setup.md#ubuntu--debian) respectively for a more detailed description on how to install all dependencies and how to build Volcanite.
To install all required dependencies under Ubuntu, first install the [Vulkan SDK](https://vulkan.lunarg.com/sdk/home), the minimal build packages with
```
sudo apt install -y build-essential cmake xorg-dev
```
and optionally the `libhdf5-dev libvtk9-dev libtiff-dev libpugixml-dev libsqlite3-dev` packages for compatibility with
a wider range of file formats.
Build the `volcanite` executable with
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
The third party open source libraries that Volcanite uses and their licenses are listed in
[Development.md](doc/Development.md#licenses).  

## About
Volcanite was created by [Max Piochowiak](https://cg.ivd.kit.edu/piochowiak/staff_index.php) with significant code
contributions by [Reiner Dolp](https://cg.ivd.kit.edu/english/staff_2590.php). Additional contributions by Fabian
Schiekel, Patrick Jaberg, and Mirco Werner. All contributors are affiliated with Karlsruhe Institute of Technology (KIT).
You can cite the following publication if you use the Volcanite CSGV compression in your projects:

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
