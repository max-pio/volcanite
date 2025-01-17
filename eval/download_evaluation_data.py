#  Copyright (C) 2024, Max Piochowiak, Karlsruhe Institute of Technology
#
#  This program is free software: you can redistribute it and/or modify
#  it under the terms of the GNU General Public License as published by
#  the Free Software Foundation, either version 3 of the License, or
#  (at your option) any later version.
#
#  This program is distributed in the hope that it will be useful,
#  but WITHOUT ANY WARRANTY; without even the implied warranty of
#  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
#  GNU General Public License for more details.
#
#  You should have received a copy of the GNU General Public License
#  along with this program.  If not, see <https://www.gnu.org/licenses/>.
from pathlib import Path
from time import sleep

from h5py.h5t import convert
from volcanite import converter as vc, clouddata as vcd, volcaniteeval as ve
import argparse

import requests
import shutil

def download_file(url: str, directory: Path, file_name: str | None = None, log: bool = True) -> Path:
    # taken from https://stackoverflow.com/questions/16694907/download-large-file-in-python-with-requests
    if file_name is None:
        file_name = url.split('/')[-1]
    directory.mkdir(parents=True, exist_ok=True)
    with requests.get(url, allow_redirects=True, stream=True) as req:
        with open(directory / Path(file_name), 'wb') as local_file:
            shutil.copyfileobj(req.raw, local_file)
    if log:
        print(f"Downloaded {url} to {directory / Path(file_name)}")
    return directory / Path(file_name)

def write_citation(directory: Path, name: str) -> None:
    """"Downloads a license for the volume [name] and writes a citation and this license in [directory]/[name].txt"""

    citations = {"azba":
('''Kenney, Justin W.; Steadman, Patrick E.; Young, Olivia et al. (2021).
A 3D Adult Zebrafish Brain Atlas (AZBA) for the Digital Age [Dataset]. Dryad.
https://doi.org/10.5061/dryad.dfn2z351g''', "https://creativecommons.org/publicdomain/zero/1.0/legalcode.txt"),
"atlas": ('''Jaus''', "https://www.apache.org/licenses/LICENSE-2.0.txt")}

    if not name in citations:
        raise ValueError("No citation found for {name}")
    ref = citations[name][0]
    url = citations[name][1]

    with requests.get(url, stream=True) as req:
        with open(directory / Path(name + ".txt"), 'w') as file:
            file.write(ref + "\n\n")
            file.write(req.text)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(
        prog='Volcanite Evaluation Data Downloader',
        description='Downloads several segmentation volumes for the Volcanite evaluation scripts.',
        epilog='')

    parser.add_argument("directory", help="Base directory in which the data set subfolders will be downloaded.")
    parser.add_argument("--keep", action="store_true", help="Keep the original files after creating the CSGV volumes.")
    parser.add_argument("--volcanite-src", help="Location of the Volcanite source directory (git repository base).")
    parser.add_argument("--big-data", help="Download large (~ 2TB) data sets as well. Use with care!")
    parser.add_argument("--overwrite", action="store_true", help="Overwrite existing volumes.")
    args = parser.parse_args()

    csgv_directory = Path(args.directory)
    if args.volcanite_src:
        volcanite_src = Path(args.volcanite_src)
    else:
        volcanite_src = Path(__file__).parent.parent
        print(f"obtained volcanite source directory from script location as {volcanite_src}")

    if not volcanite_src.exists():
        print(f"Volcanite source directory {volcanite_src} does not exist.")
        exit(1)
    config_dir = volcanite_src / Path("eval/config")
    if not config_dir.exists():
        print(f"Volcanite source directory does not contain configuration subdirectory {config_dir}.")
        exit(2)

    # write the paths to the config file
    setup_file = volcanite_src / Path("eval/setup.txt")
    if setup_file.exists():
        print(f"Overwriting setup file {setup_file}.")
        # sleep(2)
    with open(setup_file, "w") as file:
        file.write("volcanite_src: " + str(volcanite_src.absolute()) + "\n")
        file.write("config_dir: " + str(config_dir.absolute()) + "\n")
        file.write("csgv_dir: " + str(csgv_directory.absolute()) + "\n")

    # create download directory
    csgv_directory.mkdir(parents=True, exist_ok=True);

    # BUILD VOLCANITE --------------------------------------------------------------------------------------------------
    volcanite_binary_dir = ve.VolcaniteExec.build_volcanite(volcanite_src / "cmake-build-release")

    # DOWNLOADING AND COMPRESSING --------------------------------------------------------------------------------------
    print("----------- AZBA ----------- ")
    cur_dir = csgv_directory / Path("azba")
    if not cur_dir.exists() or args.overwrite:
        download_file("https://datadryad.org/api/v2/files/1098598/download", cur_dir, "azba.nii.gz")
        write_citation(csgv_directory, "azba")
        vc.convert_volume(cur_dir / "azba.nii.gz", cur_dir / "azba.hdf5")
        ve.VolcaniteExec.run_volcanite(volcanite_binary_dir, f"--headless -c {csgv_directory / "azba.csgv"} {cur_dir / "azba.hdf5"}")
    else:
        print(f"{cur_dir} already exists. Skipping download.")
