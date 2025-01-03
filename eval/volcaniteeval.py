import subprocess as subp
from enum import Enum
from pathlib import Path
import shutil
import re
from datetime import datetime
from typing import Self

class ExistingPolicy(Enum):
    ABORT = 0,
    APPEND = 1,
    MOVE = 2,
    DELETE = 3

class VolcaniteLogFile:
    """
    Encapsulates the log file into which new Volcanite evaluation results are appended.
    The initial log file will be created as a copy from log_file_template.
    If a fallback_log is given, it is appended to the log_file when a Volcanite run fails instead of aborting the
    evaluation. It may use the %name placeholder for the name of the current evaluation.
    """
    
    @classmethod
    def __create_fallback_string(cls, log_file_template: Path, replace_with: str = "") -> str | None:
        """Reads the format string from the log file template and replaces all placeholders with replace_with."""

        format_string: str = ""
        #     std::ifstream file = std::ifstream(log_file_template);
        #     if (file.is_open()) {
        #         std::string line;
        #         std::getline(file, line);
        #         while (line.starts_with("#fmt:")) {
        #             line = line.substr(5);
        #             format_string += (line + "\n");
        #             std::getline(file, line);
        #         }
        #         if (format_string.ends_with('\n'))
        #             format_string.pop_back(); // remove trailing '\n'
        #         file.close();
        #     } else {
        #         Logger(ERROR) << "Could not open pre-existing evaluation log file " << log_file_template;
        #         return 5;
        #     }

        possible_keys: list[str] = [# "name", name can be used in the fallback string
                                    "time", "args",
                                    "cr", "comp_s", "comp_mainpass_s", "comp_prepass_s", "comp_gb_per_s"
                                    "csgv_gb", "orig_gb", "volume_dim",
                                    "decomp_cpu_gb_per_s", "decomp_gpu_gb_per_s",
                                    "frame_min_ms", "frame_avg_ms", "frame_sdv_ms", "frame_med_ms", "frame_max_ms",
                                    "render_total_ms",
                                    "mem_framebuffer_mb", "mem_uniformbuffer_mb", "mem_materials_mb", "mem_encoding_ms",
                                    "mem_cache_mb", "mem_emptyspace_mb", "mem_total_mb",
                                    "render_frames"]
        for possible_key in possible_keys:
            format_string.replace("%" + possible_key, replace_with)
        return format_string

    def __init__(self, log_file: Path, log_file_template: Path):
        self.log_file: Path = log_file
        self.__log_file_template: Path = log_file_template
        if not self.__log_file_template.exists():
            raise IOError("Template log file " + str(self.__log_file_template) + " does not exist.")
        self.fallback_log: str = VolcaniteLogFile.__create_fallback_string(self.__log_file_template)

    def setup(self, old_log_policy: ExistingPolicy = ExistingPolicy.ABORT):
        """
        Ensures that the log_file exists at its location.
        :param old_log_policy: handling of existing log files, either 'abort' (default), 'append', or 'overwrite'
        """
        if self.log_file.exists():
            if old_log_policy == ExistingPolicy.ABORT:
                raise IOError("Log file " + str(self.log_file) + " exist and existing policy is 'abort'")
            elif old_log_policy == ExistingPolicy.MOVE:
                shutil.move(self.log_file, str(self.log_file.resolve()) + "_" + datetime.now().strftime("%Y%m%d-%H%M%S"))
            elif old_log_policy == ExistingPolicy.DELETE:
                self.log_file.unlink()

            if old_log_policy != ExistingPolicy.APPEND:
                shutil.copy(self.__log_file_template, self.log_file)
        else:
            shutil.copy(self.__log_file_template, self.log_file)

        if not self.log_file.exists():
            raise IOError(f"Could not create log file {self.log_file} (template {self.__log_file_template})")

    def log_manual(self, output: str, end: str = "\n") -> None:
        with open(str(self.log_file), "a") as log_out:
            log_out.write(output + end)

    def create_formatted_copy(self, dest: str, newline_separator: str = None, remove_line_prefix: str = None,
                              replace_map: dict[str, str] = None):
        """
        Copies the current log file to dest and re-formats the file:
        Removes all lines starting with the remove_line_prefix if it is given.
        Removes existing line breaks and creates new line breaks at any occurring newline_separator if it is given.
        Uses the replace_map to replace any key with its value if it is given.
        """
        if not self.log_file.exists():
            raise FileNotFoundError(f"Log file {self.log_file} does not exist")
        with open(self.log_file, 'r') as log_in:
            input_log = log_in.read()
            formatted_log = re.sub(r'^{}.*\n'.format(remove_line_prefix), '', input_log, re.MULTILINE) if newline_separator else input_log
            if newline_separator:
                formatted_log = formatted_log.replace('\n', '')
                formatted_log = formatted_log.replace('\\\\', '\n')
            for repl in replace_map.items():
                formatted_log = formatted_log.replace(repl[0], repl[1])
            with open(dest, 'w') as file_out:
                file_out.write(formatted_log)

    @classmethod
    def initialize_log_files(cls, log_files : list[Self], old_logs: ExistingPolicy = ExistingPolicy.ABORT):
        for log_file in log_files:
            log_file.setup(old_logs)


class VolcaniteEvaluation:
    """
    Encapsulates one evaluation. The evaluation results are stored in a single directory (eval_out_directory).
    :var eval_out_directory: directory to store evaluation results in
    :var log_files: list of Volcanite evaluation log files that are used in the evaluation
    :var name: name of the evaluation
    """

    def __init__(self, eval_out_directory: str, existing_policy: ExistingPolicy = ExistingPolicy.ABORT, name: str = None,
                 template_log_files: list[str] = None, new_log_file_names: list[str] = None,
                 enable_log: bool = True, dry_run: bool = False, auto_init: bool = True):
        """
        Encapsulates one evaluation. The evaluation results are stored in a single directory (eval_out_directory).
        If auto_init is True, the directory is automatically set up. Otherwise, you must call
        If the directory already exists, the existing_policy determines if the evaluation runs will raise an error,
        appends new results to the existing logs, moves the old directory to a backup path, or deletes the old directory.
        :param eval_out_directory: directory in which to store evaluation results
        :param existing_policy: how to proceed if the evaluation directory already exists
        :param name: name of the evaluation
        :param template_log_files: all templates from which Volcanite evaluation log files are created in the directory
        :param new_log_file_names: if not None, a new name for each evaluation log file in the order of the templates
        :param enable_log: if false, no log files are exported by Volcanite
        :param dry_run: if true, Volcanite calls are only printed to the command line but not executed
        :param auto_init: if True, the directory is automatically set up. Otherwise, initialize() must be called later
        """
        if new_log_file_names and len(new_log_file_names) != len(template_log_files):
            raise ValueError("new_log_file_names and template_log_files must have the same length")
        self.eval_out_directory: Path = Path(eval_out_directory)
        self.existing_policy: ExistingPolicy = ExistingPolicy.ABORT
        self.name: str = self.eval_out_directory.stem if name is None else name
        self.enable_log = enable_log
        self.dry_run = dry_run

        # create log file links
        self.log_files: list[VolcaniteLogFile] = []
        for i, tlf in enumerate(template_log_files):
            if new_log_file_names:
                self.log_files.append(VolcaniteLogFile(self.eval_out_directory / Path(new_log_file_names[i]), Path(tlf)))
            else:
                self.log_files.append(VolcaniteLogFile(self.eval_out_directory / Path(tlf).name, Path(tlf)))

        self.__initialized = False
        if auto_init:
            self.initialize()

    def initialize(self):
        create: bool = True
        if self.eval_out_directory.exists():
            if self.existing_policy == ExistingPolicy.ABORT:
                raise IOError("Evaluation directory " + str(self.eval_out_directory) + " exist and existing policy is 'abort'")
            elif self.existing_policy == ExistingPolicy.MOVE:
                shutil.move(self.eval_out_directory, str(self.eval_out_directory.resolve()) + "_" + datetime.now().strftime("%Y%m%d-%H%M%S"))
            elif self.existing_policy == ExistingPolicy.DELETE:
                shutil.rmtree(self.eval_out_directory)

            if self.existing_policy == ExistingPolicy.APPEND:
                create = False

        if create:
            # create evaluation output directory
            self.eval_out_directory.mkdir(parents=True, exist_ok=True)
            # create all log files from their templates
            for log_file in self.log_files:
                log_file.setup()

        if not self.eval_out_directory.exists():
            raise IOError(f"Could not create evaluation directory {self.eval_out_directory}")

        self.__initialized = True

    def is_initialized(self):
        return self.__initialized

    def get_log(self, filename: str):
        return next((log for log in self.log_files if log.log_file.name == filename), None)

    def get_all_logs(self):
        return self.log_files


class VolcaniteArg:
    """
    Class that encapsulates Volcanite command line arguments. VolcaniteArg also provides factory methods to create
    derived VolcaniteArgs and to parse VolcaniteArgs, mainly by concatenating VolcaniteArg identifiers in a
    deterministic order.

    :var args_encoding: CSGV encoding mode arguments
    :var args_brick_size: CSGV brick size arguments
    :var args_cache_mode: render cache mode arguments
    :var args_shading: render shading mode arguments
    :var args_default: default arguments for any evaluation run
    """

    args_encoding: dict[str, Self] = {}
    args_brick_size: dict[str, Self] = {}
    args_cache_mode: dict[str, Self] = {}
    args_shading: dict[str, Self] = {}
    args_default: dict[str, Self] = {}

    __csgv_directory: Path = None
    __vcfg_directory: Path = None
    __eval_directory: Path = None

    @classmethod
    def setup_directories(cls, csgv_directory: str, vcfg_directory: str, evaluation: VolcaniteEvaluation):
        """
        Sets static paths to directories that are referenced when creating certain VolcaniteArgs
        :param csgv_directory: directory where newly compressed CSGV files are stored (and can be re-imported from)
        :param vcfg_directory: directory containing the config files
        :param evaluation: the Volcanite evaluation specifying the evaluation output direcotry for images and videos
        """
        cls.__csgv_directory = Path(csgv_directory)
        cls.__vcfg_directory = Path(vcfg_directory)
        cls.__eval_directory = evaluation.eval_out_directory

        if not cls.__csgv_directory.exists():
            raise FileNotFoundError(f"CSGV directory {cls.__csgv_directory} not found")
        if not cls.__vcfg_directory.exists():
            raise FileNotFoundError(f"vcfg config directory {cls.__vcfg_directory} not found")
        if not cls.__eval_directory.exists():
            raise FileNotFoundError(f"Evaluation output directory {cls.__eval_directory} not found")

    @classmethod
    def get_csgv_directory(cls):
        return cls.__csgv_directory

    @classmethod
    def get_vcfg_directory(cls):
        return cls.__vcfg_directory

    @classmethod
    def get_eval_directory(cls):
        return cls.__eval_directory

    @classmethod
    def __error_if_not_initialized(cls):
        if not (cls.__csgv_directory and cls.__vcfg_directory and cls.__eval_directory):
            raise RuntimeError("VolcaniteArg static directories must be initialized before usage"
                               "(VolcaniteArg.set_directories)")

    def __init__(self, args: list[str], identifier: str, priority: float):
        """
        Encapsulates a Volcanite command line argument.

        :param args: list of space separated arguments passed to the Volcanite call
        :param identifier: short identifier of the argument used to form evaluation name strings
        :param prio: priority to sort the identifiers in the evaluation name string
        """
        self.args = args
        self.identifier = identifier
        self.prio = priority

    @classmethod
    def concat_ids(cls, args: list[Self]) -> str:
        """Create a concatenated identifier for all passed args sorted by their priority."""
        sorted_by_prio = sorted(args, key=lambda a: a.prio)
        return ''.join([a.identifier for a in sorted_by_prio])

    @classmethod
    def arg_csgv_export(cls, args: list[Self]) -> Self:
        cls.__error_if_not_initialized()
        return VolcaniteArg(["-c", str(cls.__csgv_directory) + "/" + cls.concat_ids(args) + ".csgv"], "", 1000)
    @classmethod
    def arg_csgv_import(cls, args: list[Self]) -> Self:
        cls.__error_if_not_initialized()
        return VolcaniteArg([str(cls.__csgv_directory) + "/" + cls.concat_ids(args) + ".csgv"], "", 1000)

    @classmethod
    def arg_image_export(cls, args: list[Self], filetype: str = "png") -> Self:
        cls.__error_if_not_initialized()
        return VolcaniteArg(["-i", str(cls.__eval_directory) + "/" + cls.concat_ids(args) + "." + filetype], "", 1000)

    @classmethod
    def arg_video_export(cls, args, create_dir=True) -> Self:
        cls.__error_if_not_initialized()
        video_dir = (Path(cls.__eval_directory) / Path(cls.concat_ids(args))).absolute()
        if create_dir:
            video_dir.mkdir(parents=True, exist_ok=True)
        return VolcaniteArg(["-v", str(video_dir) + "/" + cls.concat_ids(args) + "_{:04}.jpg"], "", 1000)

    @classmethod
    def arg_vcfg_import(cls, args: list[Self], resolution: str = "1920x1080") -> Self:
        cls.__error_if_not_initialized()
        return VolcaniteArg(["--config", str(cls.__vcfg_directory / Path(cls.concat_ids(args) + ".vcfg")), "--resolution", resolution], "", 1000)

    @classmethod
    def arg_rec_import(cls, args: list[Self]) -> Self:
        cls.__error_if_not_initialized()
        return VolcaniteArg(["--record-in", str(cls.__vcfg_directory / Path(cls.concat_ids(args) + ".rec"))], "", 1000)

    @classmethod
    def arg_dataset(cls, data_path: str, identifier: str | None = None,
                    chunks: tuple[int, int, int] | None = None):
        """
        Creates a VolcaniteArg for loading the data set located ata data_path.
        If chunked is not none, the data path must contain three {} placeholders for the chunk x, y, and z indices and
        chunks must be a tuple of the last inclusive x, y, and z chunk index.

        :param data_path: path of the segmentation volume data set file (csgv or volume input data)
        :param identifier: identifier of the VolcaniteArg. If None, the file name without its extension
        :param chunks: None or the last x, y, z chunk index
        :return: VolcaniteArg for the data set
        """
        if identifier is None:
            identifier = Path(data_path).stem.split('.')[0]
            if chunks:
                identifier = identifier.format("0-" + str(chunks[0]), "0-" + str(chunks[1]), "0-" + str(chunks[2]))
        if chunks:
            return VolcaniteArg([data_path, "--chunked", str(chunks[0]) + "," + str(chunks[1]) + "," + str(chunks[2])],
                                identifier, 0)
        else:
            return VolcaniteArg([data_path], identifier, 0)


# several default VolcaniteArgs:
VolcaniteArg.args_encoding = {"nibble": VolcaniteArg(["-s", "0"], "_nb", 1),
                              "nibble_ra": VolcaniteArg(["-s", "0", "-p", "-o", "pnls"], "_nb-ra", 1),
                              "rANS": VolcaniteArg(["-s", "2"], "_rans", 1),
                              "wmh_nosb": VolcaniteArg(["-s", "2", "-p", "-o", "pnl", "p"], "_wm-sb", 1),
                              "wmh": VolcaniteArg(["-s", "2", "-p", "-o" ,"pnls"], "_wm-sb", 1)}
VolcaniteArg.args_brick_size = {"16": VolcaniteArg(["-b", "16"], "_b16", 2),
                                "32": VolcaniteArg(["-b", "32"], "_b32", 2),
                                "64": VolcaniteArg(["-b", "64"], "_b64", 2)}
VolcaniteArg.args_cache_mode = {"none": VolcaniteArg(["--cache-mode", "n"], "_csh-n", 3),
                            "voxel": VolcaniteArg(["--cache-mode", "v", "--empty-space-res", "0"], "_csh-v", 3),
                            "voxel_es": VolcaniteArg(["--cache-mode", "v", "--empty-space-res", "2"], "_csh-v_es", 3),
                            "brick": VolcaniteArg(["--cache-mode", "b"], "_csh-b", 3),
                            "brick_sm": VolcaniteArg(["--cache-mode", "b", "--decode-sm"], "_csh-bsm", 3)}
VolcaniteArg.args_shading = {"local": VolcaniteArg([], "_local", 0.5),
                             "shadow": VolcaniteArg([], "_shadow", 0.5),
                             "ao": VolcaniteArg([], "_ao", 0.5),
                             "pt": VolcaniteArg([], "_pt", 0.5)}
VolcaniteArg.args_default = {"verbose": VolcaniteArg(["--verbose"], "", 1000),
                             "headless": VolcaniteArg(["--headless"], "", 1000)}


class VolcaniteExec:
    """
    Interface for compiling and executing Volcanite.
    :var volcanite_src_directory: the directory in which the Volcanite git repository is located.

    :var git_checkout: git commit or branch that is pulled and build before the first execution of Volcanite
    :var build_subdir: the build sub-directory in the specified Volcanite source directory
    """
    volcanite_src_directory = Path(__file__).parent.parent.resolve()

    def __init__(self, evaluation: VolcaniteEvaluation, git_checkout : str = "main", build_subdir : str = "cmake-build-release"):
        """
        Creates a Volcanite executor for the given evaluation.
        :param evaluation: the VolcaniteEvaluation including the evaluation output directory, name and all log files
        :param git_checkout: git commit, tag, or branch name that is checked out before building volcanite
        :param build_subdir: directory in the git repository in which Volcanite is build (default: cmake-build-release)
        """
        self.evaluation = evaluation
        self.git_checkout = git_checkout
        self.build_subdir = build_subdir
        self.__is_build = False

    def __build_dir(self) -> str:
        """Returns the absolute path to the directory in which Volcanite is build as string."""
        return str((VolcaniteExec.volcanite_src_directory / Path(self.build_subdir)).resolve())

    def build(self):
        """Checks out the git commit and builds volcanite into the configured build sub-directory."""

        if self.evaluation.dry_run:
            print("Skipping Volcanite build in dry run")
            return

        subp.run(["git", "checkout", self.git_checkout], cwd=self.__build_dir())
        res = subp.run(["git", "pull"], cwd=self.__build_dir())
        if res.returncode != 0:
            print("Error: git pull returned " + str(res.returncode))
            exit(res.returncode)
        res = subp.run(["cmake", "--build", ".", "--target", "volcanite"], cwd=self.__build_dir())
        if res.returncode != 0:
            print("Error: building volcanite returned " + str(res.returncode))
            exit(res.returncode)
        self.__is_build = True

    def exec(self, args : list[VolcaniteArg], eval_name: str = None):
        """
        Executes volcanite with the specified arguments. Volcanite is compiled first if it was not build yet.
        :param args: list of VolcaniteArgs to be passed to the Volcanite call
        :param eval_name: name of this evaluation run. can be referenced in log files as %name
        """

        if not self.__is_build:
            print("Compiling volcanite executable as Volcanite was not build yet..")
            self.build()

        # construct the volcanite call with all its arguments
        if eval_name is None:
            eval_name = VolcaniteArg.concat_ids(args)
        exec_call_args: list[str] = ["./volcanite"]
        if self.evaluation.enable_log:
            exec_call_args += ["--eval-logfiles", str(','.join([str(log.log_file.resolve())
                                                                for log in self.evaluation.log_files]))]
            if eval_name:
                exec_call_args += ["--eval-name", eval_name]

        # append all user passed arguments that are encapsulated in VolcaniteArg objects, sorted by priority
        # args example:   [VolcaniteArg(["-b", "16"], "_b16"), VolcaniteArg(["-s", "0"], "_nb")]
        args = sorted(args, key=lambda a: a.prio)
        exec_call_args = exec_call_args + [a for volcanite_arg in args for a in volcanite_arg.args]
        print("RUN VOLCANITE -----------------  " + eval_name)
        print(" ".join(exec_call_args))
        print("-------------------------------")
        if not self.evaluation.dry_run:
            res = subp.run(exec_call_args, cwd=self.__build_dir() + "/volcanite")
            if res.returncode != 0:
                print("Error: volcanite returned " + str(res.returncode))
                if self.evaluation.enable_log:
                    for log in self.evaluation.log_files:
                        if log.fallback_log:
                            print("Error: Volcanite returned " + str(res.returncode))
                            log.log_manual(log.fallback_log.replace("%name", eval_name) + "\n")
                        else:
                            raise RuntimeError("Volcanite returned " + str(res.returncode) + " and no fallback log exists for " + str(log.log_file))
                else:
                    raise RuntimeError("Volcanite returned " + str(res.returncode))

    @staticmethod
    def create_mp4(args : list[VolcaniteArg]):
        """
        Creates an mp4 video file from the single frame images that Volcanite exports using ffmpeg.
        args must contain a video output argument for which the video conversion is executed.
        :param args: volcanite arguments containing a video output arguments "-v image_path_template.xyz"
        """
        video_path = None
        for a in args:
            if "-v" in a.args:
                v_index = a.args.index("-v")
                video_path = a.args[v_index + 1]

        if video_path is None:
            print("Video args do not contain a video output argument")
        else:
            _dir = Path(video_path).parent
            _name = Path(video_path).name
            prefix = _name[:_name.find("{")]
            files = prefix + "*" + _name[_name.rfind("}")+1:]
            cmd = "ffmpeg -n -framerate 60 -pattern_type glob -i '" + files + "' -c:v libx264 -pix_fmt yuv420p " + prefix + ".mp4"
            print("Creating video file in " + str(_dir.absolute()) + " with\n  " + cmd)
            subp.run(cmd, cwd=str(_dir.absolute()), shell=True)

if __name__ == "__main__":
    # create the data set VolcaniteArgs
    arg_data = {"cells": VolcaniteArg.arg_dataset("/home/maxpio/data/ev/cells/cells_frame055.raw", "cells"),
                "fiber": VolcaniteArg.arg_dataset("/home/maxpio/data/ev/fiber/fiberpolymer_1579x1092x1651_16bit.hdf5", "fiber"),
                "h01": VolcaniteArg.arg_dataset("/home/maxpio/data/ev/h01/chunks/x{}y{}z{}.hdf5", "h01", chunks=(4,5,5)),
                "azba": VolcaniteArg.arg_dataset("/home/maxpio/data/ev/azba/AZBA.hdf5", "azba")}

    # setup the evaluation output directory and the log files
    evaluation = VolcaniteEvaluation("~/data/eval/out", ExistingPolicy.MOVE, "my_test_eval",
                                     ["~/data/tmp_logs/my_test_eval/my_test_eval.csv",
                                      "~/data/tmp_logs/my_test_eval/my_test_eval.tex"],
                                     enable_log=True, dry_run=True)

    volcanite = VolcaniteExec(evaluation, "main", "cmake-build-release")
    volcanite.build()

    for arg_shade in VolcaniteArg.args_shading.values():
        for log in evaluation.get_all_logs():
            log.log_manual(arg_shade.identifier)

        volcanite.exec([arg_shade, arg_data["cells"]])



