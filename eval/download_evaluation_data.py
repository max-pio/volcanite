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
import os.path
from time import sleep

import argparse
import numpy as np
import requests
import shutil
import zipfile_deflate64 as zipfile

from volcanite import converter as vc, converter_chunked as vcc, clouddata as vcd, volcaniteeval as ve
from volcanite.volcaniteeval import VolcaniteArg
from common import data_specific_compression_args, data_specific_rendering_args


def download_file(url: str, directory: Path, file_name: str | None = None, log: bool = True, overwrite: bool = False) -> Path | None:
    """
    Downloads the file from the url to the directory as file_name.

    :return: file path if the file already exists or the download was successful. None otherwise.
    """

    local_file_path = directory / Path(file_name)
    if log:
        print(f"Downloading {url} to {local_file_path}", end="")
    # taken from https://stackoverflow.com/questions/16694907/download-large-file-in-python-with-requests
    if file_name is None:
        file_name = url.split('/')[-1]
    directory.mkdir(parents=True, exist_ok=True)
    if os.path.isfile(local_file_path) and not overwrite:

        if not requests.head(url).headers.get('Content-Length'):
            print(f"error downloading {url}: has no content length")
            return None

        expected_size = int(requests.head(url).headers.get('Content-Length'))
        if os.path.getsize(local_file_path) == expected_size:
            if log:
                print(" skipped, already exists.")
            return local_file_path
        else:
            if log:
                print(" (overwrite)", end="")

    try:
        with requests.get(url, allow_redirects=True, stream=True) as req:
            with open(local_file_path, 'wb') as local_file:
                shutil.copyfileobj(req.raw, local_file)
        if log:
            print(" done.")
    except requests.exceptions.RequestException as e:
        print(f"error downloading {url}: {e}")
        return None

    return local_file_path

def download_files(url_fmt: str, _last_chunk: (int, int, int), directory: Path, file_name_fmt: str | None = None,
                   log: bool = True, overwrite: bool = False) -> Path:
    _chunk_files = [f"x{x}y{y}z{z}.hdf5" for x in range(0, _last_chunk[0] + 1) for y in range(0, _last_chunk[1] + 1)
                                                for z in range(0, _last_chunk[2] + 1)]
    if file_name_fmt is None:
        file_name_fmt = Path(url_fmt).name
    for z in range(0, _last_chunk[2] + 1):
        for y in range(0, _last_chunk[1] + 1):
            for x in range(0, _last_chunk[0] + 1):
                if not download_file(url_fmt.format(x, y, z), directory, file_name_fmt.format(x, y , z), log, overwrite):
                    return (-1, -1, -1)
    return _last_chunk

def write_citation(directory: Path, name: str) -> None:
    """"Downloads a license for the volume [name] and writes a citation and this license in [directory]/[name].txt"""

    citations = {
"azba": ('''Kenney, Justin W.; Steadman, Patrick E.; Young, Olivia et al. (2021).
A 3D Adult Zebrafish Brain Atlas (AZBA) for the Digital Age [Dataset]. Dryad.
https://doi.org/10.5061/dryad.dfn2z351g''', "https://creativecommons.org/publicdomain/zero/1.0/legalcode.txt"),
#
# TODO: atlas data set is stored on google drive. could download with gdown python package.
"atlas": ('''Jaus, Alexander; Seibold, Constantin; Hermann, Kelsey; Shahamiri, Negar; Walter, Alexandra; Giske, Kristina;
Haubold, Johannes; Kleesiek, Jens; Stiefelhagen, Rainer (2024). Towards Unifying Anatomy Segmentation: Automated
 Generation of a Full-Body CT Dataset. 2024 IEEE International Conference on Image Processing (ICIP),
 Abu Dhabi, United Arab Emirates, 2024, pp. 41-47, https://doi.org/10.1109/ICIP51287.2024.10647307.
 https://www.synapse.org/Synapse:syn52287632/version/1
 https://github.com/alexanderjaus/AtlasDataset''', "https://www.apache.org/licenses/LICENSE-2.0.txt"),
#
 "ara2016": ('''Allen Mouse Reference Atlas [Dataset]. bossdb archive. https://doi.org/10.60533/BOSS-2017-DDKQ''',
"https://creativecommons.org/licenses/by/4.0/legalcode.txt"),
#
"pa66": ('''Bertoldo, J., Decencière, E., Ryckelynck, D., & Proudhon, H. (2021). Glass fiber-reinforced polyamide
 66 3D X-ray computed tomography dataset for deep learning segmentation (0.0.0) [Data set]. Zenodo.
 https://doi.org/10.5281/zenodo.4587827''', "https://creativecommons.org/licenses/by-sa/4.0/legalcode.txt"),
#
"h01": ('''Alexander Shapson-Coe et al., A petavoxel fragment of human cerebral cortex reconstructed at nanoscale resolution.
Science384,eadk4858(2024).DOI:10.1126/science.adk4858
https://h01-release.storage.googleapis.com/
Data Set: gs://h01-release/data/20210601/c3/''', "https://creativecommons.org/licenses/by/4.0/legalcode.txt"),
#
"motta2019": ('''Motta A, Berning M, Boergens KM, Staffler B, Beining M, Loomba S, Hennig Ph, Wissler H, Helmstaedter M (2019).
Dense connectomic reconstruction in layer 4 of the somatosensory cortex. Science. DOI: 10.1126/science.aay3134
https://l4dense2019.brain.mpg.de/''', ""),
#
"liconn": ('''Tavakoli, M.R., Lyudchik, J., Januszewski, M. et al.
Light-microscopy-based connectomic reconstruction of mammalian brain tissue. Nature (2025).
https://doi.org/10.1038/s41586-025-08985-1

Data Set: gs://liconn-public/ExPID82_1/segmentation/231030_agg_240123''', "https://creativecommons.org/licenses/by/4.0/legalcode.txt"),
#
"wolny2020": ('''Adrian Wolny, Lorenzo Cerrone, Athul Vijayan, Rachele Tofanelli, Amaya Vilches Barro, Marion Louveaux, Christian Wenzl, Sören Strauss, David Wilson-Sánchez,
Rena Lymbouridou, Susanne S Steigleder, Constantin Pape, Alberto Bailoni, Salva Duran-Nebreda, George W Bassel, Jan U Lohmann, Miltos Tsiantis,
Fred A Hamprecht, Kay Schneitz, Alexis Maizel, Anna Kreshuk (2020)
Accurate and versatile 3D segmentation of plant tissues at cellular resolution
eLife 9:e57613  https://doi.org/10.7554/eLife.57613

https://www.biorxiv.org/content/early/2020/01/18/2020.01.17.910562

Data Set: Ovules/train/N_428_ds2x.h5 https://osf.io/x9yns/files/osfstorage
Ovules - confocal volumetric stacks with voxel size: (0.235x0.075x0.075 µm^3) (ZYX).
Courtesy of Kay Schneitz lab, School of Life Sciences, Technical University of Munich, Germany''', ""),
#
"xtm-battery": ('''Müller, S., Sauter, C., Shunmugasundaram, R. et al. Deep learning-based segmentation of
lithium-ion battery microstructures enhanced by artificially generated electrodes. Nat Commun 12, 6205 (2021).
https://doi.org/10.1038/s41467-021-26480-9

Data Set:
https://doi.org/10.3929/ethz-b-000505935
8Cycles/Electrode1/Segmentation
''', "https://creativecommons.org/licenses/by-sa/4.0/legalcode.txt"),
#
"griesser2022-validation": ('''Griesser A., Westerteiger R., Glatt E., Hagen H., and Wiegmann A., 2022:
Fiber identification validation - ground truth and results of fiber identification for generated samples, Math2Market GmbH, Validation,
https://doi.org/10.30423/Data.Math2Market-2022-02.Validation.FiberFind

Data Set: data.math2market-2022-02.validation.fiberfind/FiberFindValidation1/FiberFindVali1_Truth_labeled_2um_32bu_600x600x200.raw

https://www.math2market.com/showroom/scandata/fiberfind-nonwoven-2022-01.html
                       
Article:
Grießer, A., Westerteiger, R., Glatt, E., Hagen, H., & Wiegmann, A. (2022).
Identification and analysis of fibers in ultra-large micro-CT scans of nonwoven textiles using deep learning.
The Journal of The Textile Institute, 114(11), 1647-1657.
https://doi.org/10.1080/00405000.2022.2145429
''', "https://opendatacommons.org/licenses/by/odc_by_1.0_public_text.txt"),
#
"griesser2022-sample": ('''Griesser A., Westerteiger R., Glatt E., De Boever W., Hagen H., and Wiegmann A., 2022:
SampleC - micro-CT and fiber identification of a nonwoven sample, Math2Market GmbH, Sample-C,
https://doi.org/10.30423/Data.Math2Market-2022-02.Sample-C.FiberFind
                       
https://www.math2market.com/showroom/scandata/fiberfind-nonwoven-2022-01.html
                       
Article:
Grießer, A., Westerteiger, R., Glatt, E., Hagen, H., & Wiegmann, A. (2022).
Identification and analysis of fibers in ultra-large micro-CT scans of nonwoven textiles using deep learning.
The Journal of The Textile Institute, 114(11), 1647-1657.
https://doi.org/10.1080/00405000.2022.2145429
''', "https://opendatacommons.org/licenses/by/odc_by_1.0_public_text.txt"),
#
"cells": ('''Emerging Tumor Development by Simulating Single-cell Events
Jakob Rosenbauer, Marco Berghoff, Alexander Schug
bioRxiv 2020.08.24.264150
https://doi.org/10.1101/2020.08.24.264150

Data Set: cell_frame065.vti
''',""),
#
"fiber": ('''Maurer, J., Salaberger, D., Jerabek, M., Kastner, J., & Major, Z. (2022).
Quantitative investigation of local strain and defect formation in short glass fibre reinforced polymers using X-ray
computed tomography. Nondestructive Testing and Evaluation, 37(5), 582–600.
https://doi.org/10.1080/10589759.2022.2075865

Data Set: glassfibrereinforcedpolymer_unloaded_1579x1092x1651_2umVS_labeled_16bit.raw
''', "")
}

    if not name.lower() in citations:
        raise ValueError("No citation found for {name}")
    ref = citations[name.lower()][0]
    url = citations[name.lower()][1]
    license_text = ""

    if url:
        with requests.get(url, stream=True) as req:
            license_text = req.text
    with open(directory / Path(name + ".txt"), 'w') as file:
        file.write(ref + "\n\n")
        if license_text:
            file.write(license_text)

def download_cloud_data(dataset: str, directory: Path, output_name: str | None = None, filetype: str = "hdf5",
                        size: tuple[int, int, int] | None = None, origin: tuple[int, int, int] = None,
                        chunk_size: tuple[int, int, int] = (1024, 1024, 1024)) -> tuple[int, int, int]:
    example_data = {"h01": ("gs://h01-release/data/20210601/c3/", {"axis_order": "xyz"}),
                    "h01-c2": ("gs://h01-release/data/20210601/c2/", {"axis_order": "xyz"}),
                    "h01-class": ("gs://h01-release/data/20210601/c3/subcompartments", {"axis_order": "xyz"}),
                    "h01-bloodvessel": ("gs://h01-release/data/20210601/blood_vessels_segmented", {"axis_order": "xyz"}),
                    "witvliet2020": ("bossdb://witvliet2020/Dataset_8/segmentation", {"axis_order": "zyx"}),
                    "ara2016": ("bossdb://ara_2016/sagittal_10um/annotation_10um_2017", {"axis_order": "zyx"}),
                    "liconn": ("gs://liconn-public/ExPID82_1/segmentation/231030_agg_240123", {"axis_order": "xyz"})}
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

def __preview_arg(enable: bool, directory: Path, dataset: str):
    if not enable:
        return ""
    arg = "-i " + str(directory / Path(dataset + ".jpg"))
    arg += " " + VolcaniteArg.concat_arg_string(data_specific_rendering_args(name))
    if config_dir and (config_dir / (dataset + ".vcfg")).exists():
        arg += " --config " + str((config_dir / (dataset + ".vcfg")).absolute())
    return arg

if __name__ == '__main__':
    parser = argparse.ArgumentParser(
        prog='Volcanite Evaluation Data Downloader',
        description='Downloads several segmentation volumes for the Volcanite evaluation scripts.',
        epilog='')

    parser.add_argument("directory", help="Base directory into which the data sets will be downloaded.")
    parser.add_argument("--keep", action="store_true", help="Keep the original volume files after creating the CSGV volumes.")
    parser.add_argument("--volcanite-src", help="Location of the Volcanite source directory (git repository base).")
    parser.add_argument("--big-data", action="store_true", help="Download large (~1TB) data sets as well. Use with care!")
    parser.add_argument("--overwrite", action="store_true", help="Overwrite existing volumes.")
    parser.add_argument("--preview", action="store_true", help="Render a preview image for each data set.")
    parser.add_argument("--no-abort", action="store_true", help="Continue the script even when creating a data set fails.")
    parser.add_argument("--only", help="Only download a single data set from [azba|ara2016|pa66|wolny2020|griesser2022-validation|xtm-battery|motta2019-small|motta2019|h01-wm|liconn|griesser2022-sample].")
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
    setup_file = volcanite_src_dir / "eval" / ve.VolcaniteArg.get_path_setup_filename()
    if setup_file.exists():
        print(f"Overwriting evaluation paths file {setup_file}.")
        # sleep(2)
    with open(setup_file, "w") as file:
        file.write("volcanite-src: " + str(volcanite_src_dir.absolute()) + "\n")
        file.write("vcfg-dir: " + str(config_dir.absolute()) + "\n")
        file.write("csgv-dir: " + str(csgv_directory.absolute()) + "\n")
        file.write("entry-command: \n")
        file.write("exit-command: \n")

    # create download directory
    csgv_directory.mkdir(parents=True, exist_ok=True);

    # BUILD VOLCANITE --------------------------------------------------------------------------------------------------
    volcanite_bin_dir = ve.VolcaniteExec.build_volcanite(volcanite_src_dir / "cmake-build-release")

    # DOWNLOADING AND COMPRESSING --------------------------------------------------------------------------------------
    if not args.only or args.only.lower() == "azba":
        print("----------- AZBA ----------- ")
        name = "azba"
        cur_dir = csgv_directory / Path(name)
        if not (csgv_directory / (name + ".csgv")).exists() or args.overwrite:
            write_citation(csgv_directory, name)
            download_file("https://datadryad.org/api/v2/files/1098598/download", cur_dir, "azba.nii.gz", overwrite=args.overwrite)
            vc.convert_volume(cur_dir / "azba.nii.gz", cur_dir / "azba.hdf5")
            ret = ve.VolcaniteExec.run_volcanite(volcanite_bin_dir,
                                                f"--headless -c {csgv_directory / (name + ".csgv")}"
                                                + data_specific_compression_args(name, volume_data_dir=csgv_directory, input_file=False) + \
                                                f" {__preview_arg(args.preview, csgv_directory, name)}"
                                                f" {cur_dir / "azba.hdf5"}")
            if ret.returncode != 0:
                print(f"Volcanite compression '{' '.join(ret.args)}' returned {ret.returncode}. Aborting.")
                if not args.no_abort:
                    exit(ret.returncode)
            # cleanup
            if not args.keep:
                shutil.rmtree(cur_dir)
        else:
            print(f"{(csgv_directory / "azba.csgv")} already exists. Skipping download.")

    if not args.only or args.only.lower() == "ara2016":
        print("----------- Ara2016 ----------- ")
        name = "Ara2016"
        cur_dir = csgv_directory / Path(name)
        if not (csgv_directory / (name + ".csgv")).exists() or args.overwrite:
            write_citation(csgv_directory, name)
            last_chunk = download_cloud_data("ara2016", directory=cur_dir, output_name=name, filetype="hdf5",
                                            size=None, origin=(0,0,0), chunk_size=(512,512,512))
            ret = ve.VolcaniteExec.run_volcanite(volcanite_bin_dir,
                                                f"--headless -c {csgv_directory / (name + ".csgv")}"
                                                + data_specific_compression_args(name, volume_data_dir=csgv_directory, input_file=False) + \
                                                f" {__preview_arg(args.preview, csgv_directory, name)}"
                                                f" --chunked {last_chunk[0]},{last_chunk[1]},{last_chunk[2]}"
                                                f" {cur_dir / (name + "_x{}y{}z{}.hdf5")}")

            if ret.returncode != 0:
                print(f"Volcanite compression '{' '.join(ret.args)}' returned {ret.returncode}. Aborting.")
                if not args.no_abort:
                    exit(ret.returncode)
            # cleanup
            if not args.keep:
                shutil.rmtree(cur_dir)
        else:
            print(f"{(csgv_directory / (name + ".csgv"))} already exists. Skipping download.")


    if not args.only or args.only.lower() == "pa66":
        print("----------- GF-PA66 ----------- ")
        name = "pa66"
        cur_dir = csgv_directory / Path(name)
        if not (csgv_directory / (name + ".csgv")).exists() or args.overwrite:
            write_citation(csgv_directory, name)
            download_file("https://zenodo.org/records/4587827/files/pa66_volumes.h5", cur_dir, "pa66.h5", overwrite=args.overwrite)
            vc.write_volume(vc.reshape_memory_order(vc.read_hdf5(cur_dir / "pa66.h5", ['pa66', 'ground_truth']), 'xyz', 'zyx'), cur_dir / "pa66_segm.hdf5")
            ret = ve.VolcaniteExec.run_volcanite(volcanite_bin_dir,
                                                f"--headless -c {csgv_directory / (name + ".csgv")}"
                                                + data_specific_compression_args(name, volume_data_dir=csgv_directory, input_file=False) + \
                                                f" {__preview_arg(args.preview, csgv_directory, name)}"
                                                f" {cur_dir / "pa66_segm.hdf5"}")

            if ret.returncode != 0:
                print(f"Volcanite compression '{' '.join(ret.args)}' returned {ret.returncode}. Aborting.")
                if not args.no_abort:
                    exit(ret.returncode)
            # cleanup
            if not args.keep:
                shutil.rmtree(cur_dir)
        else:
            print(f"{(csgv_directory / (name + ".csgv"))} already exists. Skipping download.")

    if not args.only or args.only.lower() == "wolny2020":
        print("----------- Wolny2020 ----------- ")
        name = "Wolny2020"
        cur_dir = csgv_directory / Path(name)
        if not (csgv_directory / (name + ".csgv")).exists() or args.overwrite:
            write_citation(csgv_directory, name)
            # data set: Ovules N_428_ds2x
            download_file("https://osf.io/download/ghpjq/", cur_dir, "N_428_ds2x.h5", overwrite=args.overwrite)
            vc.write_volume(vc.read_volume(cur_dir / "N_428_ds2x.h5", "xyz"), cur_dir / (name + ".hdf5"))
            ret = ve.VolcaniteExec.run_volcanite(volcanite_bin_dir,
                                                f"--headless -c {csgv_directory / (name + ".csgv")}"
                                                + data_specific_compression_args(name, volume_data_dir=csgv_directory, input_file=False) + \
                                                f" {__preview_arg(args.preview, csgv_directory, name)}"
                                                f" {cur_dir / (name + ".hdf5")}")

            if ret.returncode != 0:
                print(f"Volcanite compression '{' '.join(ret.args)}' returned {ret.returncode}. Aborting.")
                if not args.no_abort:
                    exit(ret.returncode)
            # cleanup
            if not args.keep:
                shutil.rmtree(cur_dir)
        else:
            print(f"{(csgv_directory / (name + ".csgv"))} already exists. Skipping download.")


    if not args.only or args.only.lower() == "griesser2022-validation":
        print("----------- Griesser2022 small (validation) ----------- ")
        name = "Griesser2022-validation"
        cur_dir = csgv_directory / Path(name)
        if not (csgv_directory / (name + ".csgv")).exists() or args.overwrite:
            write_citation(csgv_directory, name)
            download_file("https://doi.math2market.de/s/oYGLcs8SYkLTFtS/download/data.math2market-2022-02.validation.fiberfind.zip", cur_dir, "griesser2022-validation.zip", overwrite=args.overwrite)

            # unzip a single data set
            with zipfile.ZipFile(cur_dir / "griesser2022-validation.zip", 'r') as zf:
                zf.extract("data.math2market-2022-02.validation.fiberfind/FiberFindValidation1/FiberFindVali1_Truth_labeled_2um_32bu_600x600x200.raw", cur_dir)
            shutil.move(cur_dir / "data.math2market-2022-02.validation.fiberfind/FiberFindValidation1/FiberFindVali1_Truth_labeled_2um_32bu_600x600x200.raw",
                       cur_dir / "FiberFindVali1_Truth_labeled_2um_32bu_600x600x200.raw")

            # memory map the .raw file
            volume_mm = np.memmap(cur_dir / "FiberFindVali1_Truth_labeled_2um_32bu_600x600x200.raw", dtype=np.uint32, mode='r', shape=(200,600,600))
            last_chunk = vcc.write_chunked_volume(volume_mm, f'{cur_dir / (name + "_x{}y{}z{}.hdf5")}', (512,512,512))

            ret = ve.VolcaniteExec.run_volcanite(volcanite_bin_dir,
                                                f"--headless -c {csgv_directory / (name + ".csgv")}"
                                                + data_specific_compression_args(name, volume_data_dir=csgv_directory, input_file=False) + \
                                                f" {__preview_arg(args.preview, csgv_directory, name)}"
                                                f" --chunked {last_chunk[0]},{last_chunk[1]},{last_chunk[2]}"
                                                f" {cur_dir / (name + "_x{}y{}z{}.hdf5")}")

            if ret.returncode != 0:
                print(f"Volcanite compression '{' '.join(ret.args)}' returned {ret.returncode}. Aborting.")
                if not args.no_abort:
                    exit(ret.returncode)
            # cleanup
            if not args.keep:
                shutil.rmtree(cur_dir)
        else:
            print(f"{(csgv_directory / (name + ".csgv"))} already exists. Skipping download.")

    if not args.only or args.only.lower() == "xtm-battery":
        print("----------- Mueller2021 XTM Battery [8Cycles] ----------- ")
        name = "xtm-battery"
        cur_dir = csgv_directory / Path(name)
        if not (csgv_directory / (name + ".csgv")).exists() or args.overwrite:
            write_citation(csgv_directory, name)
            download_file("https://www.research-collection.ethz.ch/bitstream/handle/20.500.11850/505935/8Cycles.zip?sequence=9&isAllowed=y", cur_dir, "8Cycles.zip", overwrite=args.overwrite)
            with zipfile.ZipFile(cur_dir / "8Cycles.zip", 'r') as zf:
                zf.extract("8Cycles.h5", cur_dir)

            vc.write_volume(vc.reshape_memory_order(vc.read_hdf5(cur_dir / "8Cycles.h5", ['Electrode1', 'Segmentation']), 'xyz', 'zyx'), cur_dir / "xtm-battery.hdf5")
            ret = ve.VolcaniteExec.run_volcanite(volcanite_bin_dir,
                                                f"--headless -c {csgv_directory / (name + ".csgv")}"
                                                + data_specific_compression_args(name, volume_data_dir=csgv_directory, input_file=False) + \
                                                f" {__preview_arg(args.preview, csgv_directory, name)}"
                                                f" {cur_dir / "xtm-battery.hdf5"}")

            if ret.returncode != 0:
                print(f"Volcanite compression '{' '.join(ret.args)}' returned {ret.returncode}. Aborting.")
                if not args.no_abort:
                    exit(ret.returncode)
            # cleanup
            if not args.keep:
                shutil.rmtree(cur_dir)
        else:
            print(f"{(csgv_directory / (name + ".csgv"))} already exists. Skipping download.")

    if not args.only or args.only.lower() == "motta2019-small":
        print("----------- Motta2019 [small] -----------")
        name = "Motta2019-small"
        cur_dir = csgv_directory / Path(name)
        if not (csgv_directory / (name + ".csgv")).exists() or args.overwrite:
            write_citation(csgv_directory, "Motta2019")
            download_file("https://l4dense2019.brain.mpg.de/webdav/mapped-segmentation-volume/x2y3z2.hdf5", cur_dir, "Motta2019_x2y3z2.hdf5", overwrite=args.overwrite)
            ret = ve.VolcaniteExec.run_volcanite(volcanite_bin_dir,
                                                f"--headless -c {csgv_directory / (name + ".csgv")}"
                                                + data_specific_compression_args(name, volume_data_dir=csgv_directory, input_file=False) + \
                                                f" {__preview_arg(args.preview, csgv_directory, name)}"
                                                f" {cur_dir / "Motta2019_x2y3z2.hdf5"}")

            if ret.returncode != 0:
                print(f"Volcanite compression '{' '.join(ret.args)}' returned {ret.returncode}. Aborting.")
                if not args.no_abort:
                    exit(ret.returncode)
            # cleanup
            if not args.keep:
                shutil.rmtree(cur_dir)
        else:
            print(f"{(csgv_directory / (name + ".csgv"))} already exists. Skipping download.")


    # Closed Source Data Sets (not publicly available)
    if not args.only or args.only.lower() == "cells":
        print("----------- Cells ----------- ")
        name = "cells"
        cur_dir = csgv_directory / Path(name)
        if not (csgv_directory / (name + ".csgv")).exists() or args.overwrite:
            if not cur_dir.exists():
                print(f"Closed source data set {name} is not publicly available. Skipping.")
            else:
                write_citation(csgv_directory, name)
                ret = ve.VolcaniteExec.run_volcanite(volcanite_bin_dir,
                                                    f"--headless -c {csgv_directory / (name + ".csgv")}"
                                                    + data_specific_compression_args(name, volume_data_dir=csgv_directory, input_file=False) + \
                                                    f" {__preview_arg(args.preview, csgv_directory, name)}"
                                                    f" {cur_dir / "cells_065.hdf5"}")

                if ret.returncode != 0:
                    print(f"Volcanite compression '{' '.join(ret.args)}' returned {ret.returncode}. Aborting.")
                    if not args.no_abort:
                        exit(ret.returncode)
                # closed source data set input files are not removed / cleaned up
        else:
            print(f"{(csgv_directory / (name + ".csgv"))} already exists. Skipping download.")

    if not args.only or args.only.lower() == "fiber":
        print("----------- Fiber (Maurer2022) ----------- ")
        name = "fiber"
        cur_dir = csgv_directory / Path(name)
        if not (csgv_directory / (name + ".csgv")).exists() or args.overwrite:
            if not cur_dir.exists():
                print(f"Closed source data set {name} is not publicly available. Skipping.")
            else:
                write_citation(csgv_directory, name)
                ret = ve.VolcaniteExec.run_volcanite(volcanite_bin_dir,
                                                    f"--headless -c {csgv_directory / (name + ".csgv")}"
                                                    + data_specific_compression_args(name, volume_data_dir=csgv_directory, input_file=False) + \
                                                    f" {__preview_arg(args.preview, csgv_directory, name)}"
                                                    f" {cur_dir / "maurer_glassfiberpolymer.hdf5"}")

                if ret.returncode != 0:
                    print(f"Volcanite compression '{' '.join(ret.args)}' returned {ret.returncode}. Aborting.")
                    if not args.no_abort:
                        exit(ret.returncode)
                # closed source data set input files are not removed / cleaned up
        else:
            print(f"{(csgv_directory / (name + ".csgv"))} already exists. Skipping download.")


    # DOWNLOADING AND COMPRESSING BIG DATA -----------------------------------------------------------------------------
    if args.big_data or args.only:
        if not args.only or args.only.lower() == "motta2019":
            print("----------- Motta2019 -----------")
            name = "Motta2019"
            cur_dir = csgv_directory / Path(name)
            if not (csgv_directory / (name + ".csgv")).exists() or args.overwrite:
                write_citation(csgv_directory, name)
                last_chunk = download_files("https://l4dense2019.brain.mpg.de/webdav/mapped-segmentation-volume/x{}y{}z{}.hdf5", (5,8,3), cur_dir, "x{}y{}z{}.hdf5", overwrite=args.overwrite)
                ret = ve.VolcaniteExec.run_volcanite(volcanite_bin_dir,
                                                    f"--headless -c {csgv_directory / (name + ".csgv")}"
                                                    + data_specific_compression_args(name, volume_data_dir=csgv_directory, input_file=False) + \
                                                    f" {__preview_arg(args.preview, csgv_directory, name)}"
                                                    f" --chunked {last_chunk[0]},{last_chunk[1]},{last_chunk[2]}"
                                                    f" {cur_dir / "x{}y{}z{}.hdf5"}")

                if ret.returncode != 0:
                    print(f"Volcanite compression '{' '.join(ret.args)}' returned {ret.returncode}. Aborting.")
                    if not args.no_abort:
                        exit(ret.returncode)
                # cleanup
                if not args.keep:
                    shutil.rmtree(cur_dir)
            else:
                print(f"{(csgv_directory / (name + ".csgv"))} already exists. Skipping download.")

        if not args.only or args.only.lower() == "h01-wm":
            print("----------- H01 [WM] ----------- ")
            name = "H01-wm"
            cur_dir = csgv_directory / Path(name)
            if not (csgv_directory / (name + ".csgv")).exists() or args.overwrite:
                write_citation(csgv_directory, "h01")
                last_chunk = download_cloud_data("h01", directory=cur_dir, output_name=name, size=(10240, 10240, 5294), origin=(133300, 262000, 0))
                ret = ve.VolcaniteExec.run_volcanite(volcanite_bin_dir,
                                                    f"--headless -c {csgv_directory / (name + ".csgv")}"
                                                    + data_specific_compression_args(name, volume_data_dir=csgv_directory, input_file=False) + \
                                                    f" {__preview_arg(args.preview, csgv_directory, name)}"
                                                    f" --chunked {last_chunk[0]},{last_chunk[1]},{last_chunk[2]}"
                                                    f" {cur_dir / (name + "_x{}y{}z{}.hdf5")}")

                if ret.returncode != 0:
                    print(f"Volcanite compression '{' '.join(ret.args)}' returned {ret.returncode}. Aborting.")
                    if not args.no_abort:
                        exit(ret.returncode)
                # cleanup
                if not args.keep:
                    shutil.rmtree(cur_dir)
            else:
                print(f"{(csgv_directory / (name + ".csgv"))} already exists. Skipping download.")

        if not args.only or args.only.lower() == "h01-bloodvessel":
            print("----------- H01 [Blood Vessel] ----------- ")
            name = "H01-bloodvessel"
            cur_dir = csgv_directory / Path(name)
            if not (csgv_directory / (name + ".csgv")).exists() or args.overwrite:
                write_citation(csgv_directory, "h01")
                # the full blood vessel volume is ~220 GB uncompressed with 1479 labels at 16b/voxel
                #
                last_chunk = download_cloud_data("h01-bloodvessel", directory=cur_dir, output_name=name)
                ret = ve.VolcaniteExec.run_volcanite(volcanite_bin_dir,
                                                    f"--headless -c {csgv_directory / (name + ".csgv")}"
                                                    + data_specific_compression_args(name, volume_data_dir=csgv_directory, input_file=False) + \
                                                    f" {__preview_arg(args.preview, csgv_directory, name)}"
                                                    f" --chunked {last_chunk[0]},{last_chunk[1]},{last_chunk[2]}"
                                                    f" {cur_dir / (name + "_x{}y{}z{}.hdf5")}")

                if ret.returncode != 0:
                    print(f"Volcanite compression '{' '.join(ret.args)}' returned {ret.returncode}. Aborting.")
                    if not args.no_abort:
                        exit(ret.returncode)
                # cleanup
                if not args.keep:
                    shutil.rmtree(cur_dir)
            else:
                print(f"{(csgv_directory / (name + ".csgv"))} already exists. Skipping download.")


        if not args.only or args.only.lower() == "liconn":
            print("----------- liconn ----------- ")
            name = "liconn"
            cur_dir = csgv_directory / Path(name)
            if not (csgv_directory / (name + ".csgv")).exists() or args.overwrite:
                write_citation(csgv_directory, name)
                last_chunk = download_cloud_data("liconn", directory=cur_dir, output_name=name)
                ret = ve.VolcaniteExec.run_volcanite(volcanite_bin_dir,
                                                    f"--headless -c {csgv_directory / (name + ".csgv")}"
                                                    + data_specific_compression_args(name, volume_data_dir=csgv_directory, input_file=False) + \
                                                    f" {__preview_arg(args.preview, csgv_directory, name)}"
                                                    f" --chunked {last_chunk[0]},{last_chunk[1]},{last_chunk[2]}"
                                                    f" {cur_dir / (name + "_x{}y{}z{}.hdf5")}")

                if ret.returncode != 0:
                    print(f"Volcanite compression '{' '.join(ret.args)}' returned {ret.returncode}. Aborting.")
                    if not args.no_abort:
                        exit(ret.returncode)
                # cleanup
                if not args.keep:
                    shutil.rmtree(cur_dir)
            else:
                print(f"{(csgv_directory / (name + ".csgv"))} already exists. Skipping download.")


        if not args.only or args.only.lower() == "griesser2022-sample":
            print("----------- Griesser2022 [nonwoven sample] ----------- ")
            name = "Griesser2022-sample"
            cur_dir = csgv_directory / Path(name)
            if not (csgv_directory / (name + ".csgv")).exists() or args.overwrite:
                write_citation(csgv_directory, name)
                # LARGE ONE: will create a single 500 GB raw file
                download_file("https://doi.math2market.de/s/tYj87SXPgXT26zS/download/data.math2market-2022-02.sample-c.fiberfind.zip", cur_dir, "griesser2022-sample.zip", overwrite=args.overwrite)

                # unzip a single data set
                with zipfile.ZipFile(cur_dir / "griesser2022-sample.zip", 'r') as zf:
                    zf.extract("data.math2market-2022-02.sample-c.fiberfind/FiberFindSampleC_labeled_2.4um_32bu_15363x3960x2112.raw", cur_dir)

                # memory map the .raw file
                volume_mm = np.memmap(cur_dir / "data.math2market-2022-02.sample-c.fiberfind/FiberFindSampleC_labeled_2.4um_32bu_15363x3960x2112.raw", dtype=np.uint32, mode='r', shape=(2112,3960,15363))
                last_chunk = vcc.write_chunked_volume(volume_mm, f'{cur_dir / (name + "_x{}y{}z{}.hdf5")}', (1024,1024,1024))

                ret = ve.VolcaniteExec.run_volcanite(volcanite_bin_dir,
                                                    f"--headless -c {csgv_directory / (name + ".csgv")}"
                                                    + data_specific_compression_args(name, volume_data_dir=csgv_directory, input_file=False) + \
                                                    f" {__preview_arg(args.preview, csgv_directory, name)}"
                                                    f" --chunked {last_chunk[0]},{last_chunk[1]},{last_chunk[2]}"
                                                    f" {cur_dir / (name + "_x{}y{}z{}.hdf5")}")

                if ret.returncode != 0:
                    print(f"Volcanite compression '{' '.join(ret.args)}' returned {ret.returncode}. Aborting.")
                    if not args.no_abort:
                        exit(ret.returncode)
                # cleanup
                if not args.keep:
                    shutil.rmtree(cur_dir)
            else:
                print(f"{(csgv_directory / (name + ".csgv"))} already exists. Skipping download.")

    print("------------------------------- ")
    print(f"done! csgv data sets are available at {csgv_directory}")
    exit(0)

