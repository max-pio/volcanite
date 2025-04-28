# PyVolcanite

This python package contains a simple interface to run the Volcanite executable through system calls and manage
Volcanite command line arguments.
The main focus of the interface is running evaluation scripts and gathering the evaluation results in formatted text files.
Additionally, the package includes utility functions to convert segmentation volume files between different formats and chunk
sizes, and to download example data sets from online cloud storages.

## Install Instructions

It is advised to install the package within a [python virtual environment (venv)](https://docs.python.org/3/library/venv.html):

### Ubuntu
```bash
python -m venv ./.venv
source .venv/bin/activate
```

### Windows 
On windows the bin folder is named `Scripts`.
```bash
python -m venv ./.venv
source .venv/Scripts/activate
```

<br />
From then on, the installation is the same for Ubuntu and Windows.

Install the volcanite package. If you prefer to not install any optional dependencies or only those for a specific
functionality, install `volcanite`, or `volcanite[converter]` instead.
```bash
pip install --upgrade pip
pip install volcanite[all]
```

## License
If not stated otherwise, the code in this repository uses a GPLv3 license.
If you require alternative licensing options, please contact the authors.
The third party open source libraries that Volcanite uses and their licenses are listed in
[Development.md](doc/Development.md#licenses).

## About
Volcanite © 2024 Max Piochowiak, Reiner Dolp

Volcanite main contributors are [Max Piochowiak](https://cg.ivd.kit.edu/piochowiak/staff_index.php) and [Reiner Dolp](https://reinerdolp.com/).
Additional contributions by Fabian Schiekel, Patrick Jaberg, and [Mirco Werner](https://github.com/MircoWerner).
All contributors are affiliated with Karlsruhe Institute of Technology (KIT).
Volcanite builds on the CSGV segmentation volume compression and renderer by Max Piochowiak.
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
