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
#
"atlas": ('''Jaus, Alexander; Seibold, Constantin; Hermann, Kelsey; Shahamiri, Negar; Walter, Alexandra; Giske, Kristina;
Haubold, Johannes; Kleesiek, Jens; Stiefelhagen, Rainer (2024). Towards Unifying Anatomy Segmentation: Automated
 Generation of a Full-Body CT Dataset. 2024 IEEE International Conference on Image Processing (ICIP),
 Abu Dhabi, United Arab Emirates, 2024, pp. 41-47, https://doi.org/10.1109/ICIP51287.2024.10647307.
 https://www.synapse.org/Synapse:syn52287632/version/1
 https://github.com/alexanderjaus/AtlasDataset''', "https://www.apache.org/licenses/LICENSE-2.0.txt"),
#
 "ara2016": ('''Allen Mouse Reference Atlas [Dataset]. bossdb archive. https://10.60533/BOSS-2017-DDKQ'''
             , "https://creativecommons.org/licenses/by/4.0/legalcode.txt")}

    if not name in citations:
        raise ValueError("No citation found for {name}")
    ref = citations[name][0]
    url = citations[name][1]

    with requests.get(url, stream=True) as req:
        with open(directory / Path(name + ".txt"), 'w') as file:
            file.write(ref + "\n\n")
            file.write(req.text)

def download_cloud_data(dataset: str, directory: Path, output_name: str | None = None, filetype: str = "hdf5",
                        size: tuple[int, int, int] | None = None, origin: tuple[int, int, int] = None,
                        chunk_size: tuple[int, int, int] = (1024, 1024, 1024)) -> tuple[int, int, int]:
    example_data = {"h01": ("gs://h01-release/data/20210601/c3/", {"axis_order": "xyz"}),
                    "witvliet2020": ("bossdb://witvliet2020/Dataset_8/segmentation", {"axis_order": "zyx"}),
                    "ara2016": ("bossdb://ara_2016/sagittal_10um/annotation_10um_2017", {"axis_order": "zyx"})}
    if dataset not in example_data:
        raise ValueError(f"Unkown cloud data set {dataset}.")
    data_set_url, data_set_cfg = example_data[dataset]

    if not output_name is None:
        if output_name:
            output_name = output_name + "_"
    else:
        output_name = args.dataset + "_"
    output_name = output_name + "x{}y{}z{}.{}"

    # obtain data set
    data = vcd.CloudDataDownload(data_set_url, data_set_cfg=data_set_cfg)
    # download all chunks from the cloud
    return data.download(output_dir=directory, output_name=output_name, output_format=filetype,
                         volume_size=size, origin=origin, chunk_size=chunk_size, continue_download=True)


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
        volcanite_src_dir = Path(args.volcanite_src)
    else:
        volcanite_src_dir = Path(__file__).parent.parent
        print(f"obtained volcanite source directory from script location as {volcanite_src_dir}")

    if not volcanite_src_dir.exists():
        print(f"Volcanite source directory {volcanite_src_dir} does not exist.")
        exit(1)
    config_dir = volcanite_src_dir / Path("eval/config")
    if not config_dir.exists():
        print(f"Volcanite source directory does not contain configuration subdirectory {config_dir}.")
        exit(2)

    # write the paths to the config file
    setup_file = volcanite_src_dir / Path("eval/setup.txt")
    if setup_file.exists():
        print(f"Overwriting setup file {setup_file}.")
        # sleep(2)
    with open(setup_file, "w") as file:
        file.write("volcanite_src: " + str(volcanite_src_dir.absolute()) + "\n")
        file.write("config_dir: " + str(config_dir.absolute()) + "\n")
        file.write("csgv_dir: " + str(csgv_directory.absolute()) + "\n")

    # create download directory
    csgv_directory.mkdir(parents=True, exist_ok=True);

    # BUILD VOLCANITE --------------------------------------------------------------------------------------------------
    volcanite_bin_dir = ve.VolcaniteExec.build_volcanite(volcanite_src_dir / "cmake-build-release")

    # DOWNLOADING AND COMPRESSING --------------------------------------------------------------------------------------
    print("----------- AZBA ----------- ")
    cur_dir = csgv_directory / Path("azba")
    if not (csgv_directory / "azba.csgv").exists() or args.overwrite:
        download_file("https://datadryad.org/api/v2/files/1098598/download", cur_dir, "azba.nii.gz")
        write_citation(csgv_directory, "azba")
        vc.convert_volume(cur_dir / "azba.nii.gz", cur_dir / "azba.hdf5")
        ve.VolcaniteExec.run_volcanite(volcanite_bin_dir, f"--headless -c {csgv_directory / "azba.csgv"} {cur_dir / "azba.hdf5"}")
        # cleanup
        if not args.keep:
            shutil.rmtree(cur_dir)
    else:
        print(f"{(csgv_directory / "azba.csgv")} already exists. Skipping download.")


    print("----------- Ara2016 ----------- ")
    cur_dir = csgv_directory / Path("ara2016")
    if not  (csgv_directory / "ara2016.csgv").exists() or args.overwrite:
        last_chunk = download_cloud_data("ara2016", directory=cur_dir, output_name="ara16", filetype="hdf5",
                                         size=None, origin=(0,0,0), chunk_size=(1024,1024,1024))
        write_citation(csgv_directory, "ara2016")
        ve.VolcaniteExec.run_volcanite(volcanite_bin_dir,
                                       f"--headless --chunked {last_chunk[0]},{last_chunk[1]},{last_chunk[2]} "
                                       f"-c {csgv_directory / "ara2016.csgv"} {cur_dir / "ara16_x{}y{}z{}.hdf5"}")
        # cleanup
        if not args.keep:
            shutil.rmtree(cur_dir)
    else:
        print(f"{(csgv_directory / "ara2016.csgv")} already exists. Skipping download.")

    # DOWNLOADING AND COMPRESSING BIG DATA -----------------------------------------------------------------------------
    if args.big_data:
        exit(0)

    exit(0)

