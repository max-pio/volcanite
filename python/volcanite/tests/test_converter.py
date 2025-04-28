import pytest
from volcanite import converter as vc, volcaniteeval as ve
import numpy as np
from pathlib import Path
import PIL.Image as Image
import subprocess as subp

# global config
output_dir = Path("./out")
keep_files = False

def create_dummy_volume() -> np.ndarray:
    dummyvol = np.zeros(shape=(4, 10, 15), dtype='uint32')
    for x in range(dummyvol.shape[2]):
        dummyvol[0, 0, x] = 1
    for y in range(dummyvol.shape[1]):
        dummyvol[0, y, 0] = 2
    for z in range(dummyvol.shape[0]):
        dummyvol[z, 0, 0] = 3
    dummyvol[dummyvol.shape[0] - 1, dummyvol.shape[1] - 1, dummyvol.shape[2] - 1] = 4
    return dummyvol


def test_convert() -> None:
    # config
    file_types = ["vraw", "raw", "hdf5", "h5", "npy", "npz", "nii", "nii.gz", "vti"]
    output_dir.mkdir(parents=True, exist_ok=True)

    dummy = create_dummy_volume()

    # write and read each file, check for equality
    for ft in file_types:
        print(f"Converting {ft}")

        volume_file = output_dir / f"test_volume.{ft}"
        vc.write_volume(dummy, volume_file)
        roundtrip = vc.read_volume(volume_file)

        assert np.array_equal(dummy, roundtrip)

def test_render_volcanite() -> None:
    file_types = ["vraw", "hdf5"]
    output_dir.mkdir(parents=True, exist_ok=True)


    volcanite_src_dir = Path(subp.Popen(["git", "rev-parse", "--show-toplevel"], stdout=subp.PIPE).communicate()[0].rstrip().decode('utf-8'))
    if volcanite_src_dir.exists() and volcanite_src_dir.is_dir():
        print(f"obtained volcanite git base directory {volcanite_src_dir} with 'git rev-parse --show-toplevel'")
    else:
        print(f"could not obtain volcanite git base directory with 'git rev-parse --show-toplevel' ( {volcanite_src_dir})")
        volcanite_src_dir = Path(__file__).parent.parent.parent.parent
        print(f"obtained volcanite source directory from script location as {volcanite_src_dir}")

    if not (volcanite_src_dir.exists() and volcanite_src_dir.is_dir()):
        print("WARNING: could not obtain Volcanite source directory. Skipping rendering tests.")
        return

    volcanite_bin_dir = ve.VolcaniteExec.build_volcanite(volcanite_src_dir / "cmake-build-release")

    dummy = create_dummy_volume()
    image_files = []
    image_files_corrupt = []

    # create two png file for each file type: from the correctly exported volume, from a volume with broken memory order
    for ft in file_types:
        # correct version (write the zyx numpy array to file)
        volume_file = output_dir / f"test_volume.{ft}"
        vc.write_volume(dummy, volume_file)

        image_file = output_dir / f"test_volume_{ft}.png"
        image_files.append(image_file)
        ve.VolcaniteExec.run_volcanite(volcanite_bin_dir, f"--headless -r 240x135 -i {image_file.absolute()} {volume_file.absolute()}")

        # broken files (write xyz numpy array to file, which is not the shape that the writers expect)
        volume_file_corrupt = output_dir / f"test_volume_corrupt.{ft}"
        vc.write_volume(vc.reshape_memory_order(dummy, "zyx", "xyz"), volume_file_corrupt)

        image_file_corrupt = output_dir / f"test_volume_{ft}_corrupt.png"
        image_files_corrupt.append(image_file_corrupt)
        ve.VolcaniteExec.run_volcanite(volcanite_bin_dir, f"--headless -r 240x135 -i {image_file_corrupt.absolute()} {volume_file_corrupt.absolute()}")

    # compare correct image files for equality
    for i in range(len(image_files)):
        img_a = np.asarray(Image.open(image_files[i]), dtype='float32') / 255.
        img_a_corrupt = np.asarray(Image.open(image_files_corrupt[i]), dtype='float32') / 255.

        # the corrupted array should create a rendering that is more "noisy"
        assert img_a.shape == img_a_corrupt.shape and np.sum(np.gradient(img_a_corrupt)) > np.sum(np.gradient(img_a))

        for n in range(i + 1, len(image_files)):
            print(f"Comparing Volcanite render {image_files[i].name} vs. {image_files[n].name}")

            # images created from the presumably correct volumes (should have a small difference)
            img_b = np.asarray(Image.open(image_files[n]), dtype='float32') / 255.
            assert img_a.shape == img_b.shape
            image_diff = np.sum(img_a - img_b) / (img_a.shape[0] * img_a.shape[1])
            assert image_diff < 0.01

            # compare correct volume in img_a with corrupt volume in img_b_corrupt (should have a higher difference)
            img_b_corrupt = np.asarray(Image.open(image_files_corrupt[n]), dtype='float32') / 255.
            assert img_a.shape == img_b_corrupt.shape
            image_diff_corrupt = np.sum(img_a - img_b_corrupt) / (img_a.shape[0] * img_a.shape[1])
            assert image_diff_corrupt >= image_diff

def test_reshape_axis() -> None:
    dummy = create_dummy_volume()

    # no-op
    assert np.array_equal(dummy, vc.reshape_memory_order(dummy, 'zyx', 'zyx'))

    roundtrips = [['xyz'],
                  ['yzx', 'zxy'],
                  ['zxy', 'yzx', 'yxz']]

    for trip in roundtrips:
        result = dummy
        last_order = 'zyx'
        for i, axes in enumerate(trip):
            result = vc.reshape_memory_order(result, last_order, axes)
            last_order = axes
        result = vc.reshape_memory_order(result, last_order, 'zyx')
        assert np.array_equal(dummy, result)

@pytest.fixture(autouse=True)
def cleanup_tmpdir() -> None:
    # execute test
    yield

    # cleanup output directory
    if keep_files:
        return
    cleanup_filetypes = [".vraw", ".raw", ".hdf5", ".h5", ".npy", ".npz", ".nii", ".nii.gz", ".vti", ".png", ".csgv"]
    for f in output_dir.iterdir():
        if f.is_file() and any(f.name.endswith(ft) for ft in cleanup_filetypes):
            f.unlink()

