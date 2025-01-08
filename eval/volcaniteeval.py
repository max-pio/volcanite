import subprocess as subp
from enum import Enum
from os import PathLike
from pathlib import Path
import shutil
import re
from datetime import datetime
from time import sleep
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

    def __create_fallback_string(self, replace_with: str = "") -> str | None:
        """Reads the format string from the log file and replaces all placeholders with replace_with."""
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
        format_string = '\n'.join(self.__fmt_strs)
        for possible_key in possible_keys:
            format_string = format_string.replace("%" + possible_key, replace_with)
        return format_string

    @classmethod
    def get_fmt_and_remainder_lines_from_file(cls, log_file: Path) -> tuple[list[str], list[str]]:
        """
        Obtains the format strings from the first [0, N) lines starting with '#fmt:' and the remaining lines [N, ..].
        If the file does not contain evaluatoin results yet, the remaining lines can be assumed to be the header lines.
        :return: a list of format strings and header / remaining lines from the file.
        """
        if not log_file.exists():
            raise IOError("File " + str(log_file) + " does not exist.")

        with open(log_file, "r") as f:
            lines = f.read().split("\n")

        format_strings: list[str] = []
        i = 0
        while lines[i].startswith("#fmt:"):
            format_strings.append(lines[i][5:])
            i += 1
        header_strings: list[str] = lines[i::]
        return format_strings, header_strings

    def __init__(self, log_file: Path, fmt_strs: list[str], header_strings: list[str],
                 fallback_log_line: str | None, use_fmt_from_existing_log: bool = True):
        """
        Creates a Volcanite log file handle.
        :param log_file: path at which to store the log file.
        :param fmt_strs: the format strings used in the log file.
        :param header_strings: the header lines stored after the fmt strings in the log file at initialization.
        :param fallback_log_line: line written to log_file if a Volcanite run fails. May only use {name} key.
        :param use_fmt_from_existing_log: if True: use the fmt strings from the current log file if it already exists.
        """
        self.log_file: Path = log_file
        self.__fmt_strs = fmt_strs
        self.__header_strings = header_strings

        self.fallback_log: str = ""
        if use_fmt_from_existing_log and self.log_file.exists():
            self.__fmt_strs, _ = VolcaniteLogFile.get_fmt_and_remainder_lines_from_file(self.log_file)
        self.__fallback_log = fallback_log_line if fallback_log_line is not None else self.__create_fallback_string()
        self.disable_manual_logs = False

    @classmethod
    def create_from_template_log_file(cls, log_file: Path, template_log_file: Path,
                                      fallback_log_line: str | None, use_fmt_from_existing_log: bool = True) -> Self:
        format_strings, header_strings = cls.get_fmt_and_remainder_lines_from_file(template_log_file)
        return cls(log_file=log_file, fmt_strs=format_strings, header_strings=header_strings,
                   fallback_log_line=fallback_log_line, use_fmt_from_existing_log=use_fmt_from_existing_log)

    def get_log_file(self) -> Path:
        return self.log_file

    def get_fmt_and_header_lines(self) -> tuple[list[str], list[str]]:
        return self.__fmt_strs, self.__header_strings

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
                with(open(self.log_file, "w")) as f:
                    f.writelines(self.__fmt_strs)
                    f.writelines(self.__header_strings)
        else:
            with(open(self.log_file, "w")) as f:
                f.writelines("#fmt:" + line + "\n" for line in self.__fmt_strs)
                f.writelines(line + "\n" for line in self.__header_strings)

        if not self.log_file.exists():
            raise IOError(f"Could not create log file {self.log_file}")

    def log_manual(self, output: str, end: str = "\n") -> None:
        if self.disable_manual_logs:
            return
        with open(str(self.log_file), "a") as log_out:
            log_out.write(output + end)

    def create_formatted_copy(self, dest: Path, newline_separator: str = None, remove_line_prefixes: list[str] = None,
                              replace_map: dict[str, str] = None):
        """
        Copies the current log file to dest and re-formats the file:
        Removes all lines starting with one of the remove_line_prefixes if given.
        Removes existing line breaks and creates new line breaks at any occurring newline_separator if it is given.
        Uses the replace_map to replace any key with its value if it is given.
        """
        if not self.log_file.exists():
            raise FileNotFoundError(f"Log file {self.log_file} does not exist")
        with open(self.log_file, 'r') as log_in:
            formatted_log = log_in.read()
            # remove all lines starting with any of the remove_line_prefixes:
            if remove_line_prefixes:
                for remove_line_prefix in remove_line_prefixes:
                    formatted_log = re.sub(r"^{}.*\n".format(remove_line_prefix), "", formatted_log, flags=re.MULTILINE)
            # remove all existing newlines, replace all newline_separators with a newline:
            if newline_separator:
                formatted_log = formatted_log.replace("\n", "")
                formatted_log = formatted_log.replace(newline_separator, "\n")
            # replace all keys with the
            if replace_map:
                for repl in replace_map.items():
                    formatted_log = formatted_log.replace(repl[0], repl[1])
            with open(dest, 'w') as file_out:
                file_out.write(formatted_log)
            print(f"create formated copy of {self.log_file} to {dest}")

    @classmethod
    def initialize_log_files(cls, log_files : list[Self], old_logs: ExistingPolicy = ExistingPolicy.ABORT):
        for log_file in log_files:
            log_file.setup(old_logs)

class VolcaniteLogFileCfg:
    def __init__(self, log_file_name: str | None, fmts: list[str] | None = None, headers: list[str] | None = None,
                 template_log_file: Path | None = None, fallback_log_line: str | None = None,
                 use_fmt_from_existing_log: bool = True):
        """
        Specifies initializion of a Volcanite log file within an evaluation directory.
        If template_Log_file is given:\n
         * the format and header strings are read from that file.
         * If no log_file_name is given, the file name of the template log file is used
        If no template_log_file is given, the log_file_name, fmt_strings, and header_strings must be set.
        :param log_file_name: name of the log file in the evaluation directory
        :param fmts: list of format strings for initializing the log file
        :param headers: list of header strings for initializing the log file
        :param template_log_file: template log file from which the fmt_strings and header_strings are read
        :param fallback_log_line: line that is logged to file if Volcanite run fails. may only use {name} key
        :param use_fmt_from_existing_log: if the fmt strings from an existing log file are used if logs are appended
        """
        if template_log_file is None:
            if log_file_name is None or fmts is None or headers is None:
                raise ValueError("log_file_name and fmt_strings and header_strings must not be None when no"
                                 " template_log_file is given.")
        else:
            if fmts or headers:
                raise ValueError("fmt_strings and header_strings cannot be used in combination with template_log_file")
            if not template_log_file.exists():
                raise FileNotFoundError(f"Template log file {template_log_file} does not exist.")
        self.log_file_name = log_file_name
        self.fmt_strs = fmts
        self.header_strings = headers
        self.template_log_file = template_log_file
        self.fallback_log_line = fallback_log_line
        self.use_fmt_from_existing_log = use_fmt_from_existing_log

class VolcaniteEvaluation:
    """
    Encapsulates one evaluation. The evaluation results are stored in a single directory (eval_out_directory).
    :var eval_out_directory: directory to store evaluation results in
    :var log_files: list of Volcanite evaluation log files that are used in the evaluation
    :var name: name of the evaluation
    """

    def __init__(self, eval_out_directory: PathLike, existing_policy: ExistingPolicy = ExistingPolicy.ABORT,
                 eval_name: str = None, log_files: list[VolcaniteLogFileCfg] = None,
                 enable_log: bool = True, dry_run: bool = False, auto_init: bool = True):
        """
        Encapsulates one evaluation. The evaluation results are stored in a single directory (eval_out_directory).
        If auto_init is True, the directory is automatically set up. Otherwise, you must call
        If the directory already exists, the existing_policy determines if the evaluation runs will raise an error,
        appends new results to the existing logs, moves the old directory to a backup path, or deletes the old directory.
        :param eval_out_directory: directory in which to store evaluation results
        :param existing_policy: how to proceed if the evaluation directory already exists
        :param eval_name: name of the evaluation
        :param log_files: specifies which Volcanite evaluation log files are created and/or used in the directory
        :param enable_log: if false, no log files are exported by Volcanite
        :param dry_run: if true, Volcanite calls are only printed to the command line but not executed
        :param auto_init: if True, the directory is automatically set up. Otherwise, initialize() must be called later
        """
        self.eval_out_directory: Path = Path(eval_out_directory).resolve()
        self.existing_policy: ExistingPolicy = existing_policy
        self.name: str = self.eval_out_directory.stem if eval_name is None else eval_name
        self.log_file_configs: list[VolcaniteLogFileCfg] = log_files
        self.log_files: list[VolcaniteLogFile] | None = None
        self.enable_log: bool = enable_log
        self.dry_run: bool = dry_run

        self.__initialized = False
        if auto_init:
            self.initialize()

    def initialize(self):
        if self.dry_run:
            print("Skipping evaluation initialization in dry run")
            create: bool = False
        else:
            create: bool = True
            if self.eval_out_directory.exists():
                if self.existing_policy == ExistingPolicy.ABORT:
                    raise IOError("Evaluation directory " + str(self.eval_out_directory) + " exist and existing policy is 'abort'")
                elif self.existing_policy == ExistingPolicy.MOVE:
                    new_location = str(self.eval_out_directory.resolve()) + "_" + datetime.now().strftime("%Y%m%d-%H%M%S")
                    print("Moving existing directory " + str(self.eval_out_directory) + " to " + new_location)
                    shutil.move(self.eval_out_directory, new_location)
                elif self.existing_policy == ExistingPolicy.DELETE:
                    print("Deleting existing evaluation directory.. " + str(self.eval_out_directory))
                    sleep(5)
                    shutil.rmtree(self.eval_out_directory)

                if self.existing_policy == ExistingPolicy.APPEND:
                    print("Appending results to directory " + str(self.eval_out_directory))
                    create = False

        # setup log files
        self.log_files: list[VolcaniteLogFile] = []
        for cfg in self.log_file_configs:
            if cfg.template_log_file is None:
                self.log_files.append(VolcaniteLogFile(log_file = self.eval_out_directory / cfg.log_file_name,
                                                       fmt_strs=cfg.fmt_strs, header_strings=cfg.header_strings,
                                                       fallback_log_line=cfg.fallback_log_line,
                                                       use_fmt_from_existing_log=cfg.use_fmt_from_existing_log))
            else:
                self.log_files.append(VolcaniteLogFile.create_from_template_log_file(log_file=self.eval_out_directory / cfg.log_file_name,
                                                                                     template_log_file=cfg.template_log_file,
                                                                                     fallback_log_line=cfg.fallback_log_line,
                                                                                     use_fmt_from_existing_log=cfg.use_fmt_from_existing_log))
            if self.dry_run:
                self.log_files[-1].disable_manual_logs = True

        if create:
            # create evaluation output directory
            self.eval_out_directory.mkdir(parents=True, exist_ok=True)
            # create all log files from their templates
            for log_file in self.log_files:
                log_file.setup()

        if not self.dry_run and not self.eval_out_directory.exists():
            raise IOError(f"Could not create evaluation directory {self.eval_out_directory}")

        self.__initialized = True

        # automatically register this evaluation with the VolcaniteArgs if it has none
        if VolcaniteArg.get_eval_directory() is None:
            VolcaniteArg.setup_directories(veval=self, csgv_directory=VolcaniteArg.get_csgv_directory(),
                                           vcfg_directory=VolcaniteArg.get_vcfg_directory())

    def is_initialized(self) -> bool:
        return self.__initialized

    def get_log(self, filename: str = None) -> VolcaniteLogFile:
        """
        :return: the first log file in the log file lists or the log file handle for the given file name
        """
        if filename is None:
            return self.log_files[0]
        return next((_log for _log in self.log_files if _log.log_file_name.name == filename), None)

    def get_all_logs(self) -> list[VolcaniteLogFile]:
        return self.log_files


class VolcaniteArg:
    """
    Class that encapsulates Volcanite command line arguments. VolcaniteArg also provides factory methods to create
    derived VolcaniteArgs and to parse VolcaniteArgs, mainly by concatenating VolcaniteArg identifiers in a
    deterministic order.

    :var args_encoding: CSGV encoding mode arguments
    :var args_brick_size: CSGV brick size arguments ["16","32","64"]
    :var args_cache_mode: render cache mode arguments
    :var args_shading: render shading mode arguments
    :var args_default: default arguments for any evaluation run
    :var args_datasynth: synthetic data sets with increasing label region size (decreasing label density)
    """

    args_encoding: dict[str, Self] = {}
    args_brick_size: dict[str, Self] = {}
    args_cache_mode: dict[str, Self] = {}
    args_shading: dict[str, Self] = {}
    args_default: dict[str, Self] = {}
    args_datasynth: dict[str, Self] = {}

    __csgv_directory: Path = None
    __vcfg_directory: Path = None
    __eval_directory: Path = None

    @classmethod
    def setup_directories(cls, veval: VolcaniteEvaluation | None = None,
                          csgv_directory: PathLike | None = None, vcfg_directory: PathLike | None = None):
        """
        Sets static paths to directories that are referenced when creating certain VolcaniteArgs.
        :param csgv_directory: directory where newly compressed CSGV files are exported to and imported from
        :param vcfg_directory: directory containing config and rec files for the argument sets
        :param veval: the Volcanite evaluation specifying the evaluation output directory for images and videos
        """
        if not veval:
            raise ValueError("VolcaniteEvaluation must not be None")

        cls.__csgv_directory = Path(csgv_directory) if csgv_directory else None
        cls.__vcfg_directory = Path(vcfg_directory) if vcfg_directory else None
        cls.__eval_directory = veval.eval_out_directory if veval else None

        if cls.__csgv_directory and not cls.__csgv_directory.exists():
            raise FileNotFoundError(f"CSGV directory {cls.__csgv_directory} not found")
        if cls.__vcfg_directory and not cls.__vcfg_directory.exists():
            raise FileNotFoundError(f"vcfg config directory {cls.__vcfg_directory} not found")
        if cls.__eval_directory and not cls.__eval_directory.exists():
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
        if cls.__csgv_directory is None:
            raise RuntimeError("VolcaniteArg static csgv directory must be initialized before usage"
                               "(VolcaniteArg.set_directories)")
        return cls(["-c", str(cls.__csgv_directory) + "/" + cls.concat_ids(args) + ".csgv"], "", 1000)
    @classmethod
    def arg_csgv_import(cls, args: list[Self]) -> Self:
        if cls.__csgv_directory is None:
            raise RuntimeError("VolcaniteArg static csgv directory must be initialized before usage"
                               "(VolcaniteArg.set_directories)")
        return cls([str(cls.__csgv_directory) + "/" + cls.concat_ids(args) + ".csgv"], "", 1000)

    @classmethod
    def arg_image_export(cls, args: list[Self], filetype: str = "png") -> Self:
        if cls.__eval_directory is None:
            raise RuntimeError("VolcaniteArg static evaluation directory must be initialized before usage"
                               "(VolcaniteArg.set_directories)")
        return cls(["-i", str(cls.__eval_directory) + "/" + cls.concat_ids(args) + "." + filetype], "", 1000)

    @classmethod
    def arg_video_export(cls, args, create_dir=True) -> Self:
        if cls.__eval_directory is None:
            raise RuntimeError("VolcaniteArg static evaluation directory must be initialized before usage"
                               "(VolcaniteArg.set_directories)")
        video_dir = (Path(cls.__eval_directory) / Path(cls.concat_ids(args))).absolute()
        if create_dir:
            video_dir.mkdir(parents=True, exist_ok=True)
        return cls(["-v", str(video_dir) + "/" + cls.concat_ids(args) + "_{:04}.jpg"], "", 1000)

    @classmethod
    def arg_vcfg_import(cls, args: list[Self], resolution: str = "1920x1080") -> Self:
        if cls.__vcfg_directory is None:
            raise RuntimeError("VolcaniteArg static vcfg directory must be initialized before usage"
                               "(VolcaniteArg.set_directories)")
        return cls(["--config", str(cls.__vcfg_directory / Path(cls.concat_ids(args) + ".vcfg")), "--resolution", resolution], "", 1000)

    @classmethod
    def arg_rec_import(cls, args: list[Self]) -> Self:
        if cls.__vcfg_directory is None:
            raise RuntimeError("VolcaniteArg static vcfg directory must be initialized before usage"
                               "(VolcaniteArg.set_directories)")
        return cls(["--record-in", str(cls.__vcfg_directory / Path(cls.concat_ids(args) + ".rec"))], "", 1000)

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
            return cls([data_path, "--chunked", str(chunks[0]) + "," + str(chunks[1]) + "," + str(chunks[2])],
                                identifier, 0)
        else:
            return cls([data_path], identifier, 0)


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
VolcaniteArg.args_datasynth = {"8": VolcaniteArg(["#synth_1024x1024x1024_r6x6x6-10x10x10"], "dSynth8", 0),
                            "32": VolcaniteArg(["#synth_1024x1024x1024_r24x24x24-40x40x40"], "dSynth32", 0),
                            "128": VolcaniteArg(["#synth_1024x1024x1024_r96x96x96-160x160x160"], "dSynth128", 0),
                            "512": VolcaniteArg(["#synth_1024x1024x1024_r384x384x384-640x640x640"], "dSynth512", 0)}
VolcaniteArg.args_default = {"verbose": VolcaniteArg(["--verbose"], "", 1000),
                             "headless": VolcaniteArg(["--headless"], "", 1000)}

class VolcaniteExec:
    """
    Interface for compiling and executing Volcanite.
    :var volcanite_src_directory: the directory in which the Volcanite git repository is located
    :var evaluation: the VolcaniteEvaluation including the evaluation output directory, name and all log files
    :var git_checkout: git commit or branch that is pulled and build before the first execution of Volcanite
    :var build_subdir: the build sub-directory in the specified Volcanite source directory
    """
    volcanite_src_directory = Path(__file__).parent.parent.resolve()

    @staticmethod
    def run_log(call_args: list[str], print_log=True, *args, **kwargs):
        if print_log:
            print("> " + " ".join(call_args))
        return subp.run(call_args, *args, **kwargs)

    def __init__(self, evaluation: VolcaniteEvaluation, git_checkout : str | None = None, build_subdir : str = "cmake-build-release"):
        """
        Creates a Volcanite executor for the given evaluation.
        :param evaluation: the VolcaniteEvaluation including the evaluation output directory, name and all log files
        :param git_checkout: git commit, tag, or branch name that is checked out before building volcanite
        :param build_subdir: directory in the git repository in which Volcanite is build (default: cmake-build-release)
        """
        # build directory must have a depth of one
        if len(Path(build_subdir).parents) != 1:
            raise ValueError("Volcanite build sub-directory must have a path depth of one")

        self.evaluation = evaluation
        self.git_checkout = git_checkout
        self.build_subdir = build_subdir
        self.__is_build = False

    def info_str(self):
        return (f"{datetime.now().strftime("%Y.%m.%d-%H:%M:%S")} [{self.evaluation.name}]"
                f" exe:{"off" if self.evaluation.dry_run else "on"} log:{"on" if self.evaluation.log_files else "off"}"
                f" build {str(self.git_checkout) + "@" if self.git_checkout else ""}{self.__build_dir()}")

    def logs_info_str(self):
        log_strs = []
        for l in self.evaluation.log_files:
            log_strs.append(f"{l.get_log_file()} [{' '.join(l.get_fmt_and_header_lines()[0])}] fallback: '{l.fallback_log}'")
        return log_strs

    def __build_dir(self) -> Path:
        """Returns the absolute path to the directory in which Volcanite is build as string."""
        return VolcaniteExec.volcanite_src_directory / Path(self.build_subdir)

    def checkout_and_build(self):
        """Checks out the git commit (if configured) and builds volcanite into the configured build sub-directory."""

        if self.evaluation.dry_run:
            print("Skipping Volcanite build in dry run")
            return

        if self.git_checkout:
            self.run_log(["git", "checkout", self.git_checkout], cwd=VolcaniteExec.volcanite_src_directory)
            res = self.run_log(["git", "pull"], cwd=VolcaniteExec.volcanite_src_directory)
            if res.returncode != 0:
                raise RuntimeError(f"Error: git pull returned {res.returncode}")

        if not self.__build_dir().exists():
            self.__build_dir().mkdir(parents=True, exist_ok=True)
            build_type = "-DCMAKE_BUILD_TYPE=Debug" if "deb" in self.build_subdir.lower() else "-DCMAKE_BUILD_TYPE=Release"
            res = self.run_log(["cmake", build_type, ".."], cwd=self.__build_dir())
            if res.returncode != 0:
                raise RuntimeError(f"Error: cmake returned {res.returncode}")

        res = self.run_log(["cmake", "--build", ".", "-j", "--target", "volcanite"], cwd=self.__build_dir())
        if res.returncode != 0:
            raise RuntimeError(f"Error: building target volcanite returned {res.returncode}")
        self.__is_build = True

    def exec(self, args : list[VolcaniteArg], eval_name: str = None, headless: bool = True):
        """
        Executes volcanite with the specified arguments. Volcanite is compiled first if it was not build yet.
        :param args: list of VolcaniteArgs to be passed to the Volcanite call
        :param eval_name: name of this evaluation run. can be referenced in log files as %name
        :param headless: if true, the --headless argument is added to the execution
        """

        if not self.__is_build and not self.evaluation.dry_run:
            print("Compiling volcanite executable as Volcanite was not build yet..")
            self.checkout_and_build()

        # construct the volcanite call with all its arguments
        if eval_name is None:
            eval_name = VolcaniteArg.concat_ids(args)
        exec_call_args: list[str] = ["./volcanite"]
        if headless:
            exec_call_args += ["--headless"]
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
            res = self.run_log(exec_call_args, print_log=False, cwd=self.__build_dir() / Path("volcanite"))
            if res.returncode != 0:
                print("Error: volcanite returned " + str(res.returncode))
                if self.evaluation.enable_log:
                    for log in self.evaluation.log_files:
                        if log.fallback_log:
                            print("Error: Volcanite returned " + str(res.returncode))
                            log.log_manual(log.fallback_log.replace("%name", eval_name) + "\n")
                        else:
                            raise RuntimeError(f"Volcanite returned {res.returncode} and no fallback log exists for {log.log_file_name}")
                else:
                    raise RuntimeError(f"Volcanite returned {res.returncode}")

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
            cmd = ["ffmpeg -n -framerate 60 -pattern_type glob -i '" + files + "' -c:v libx264 -pix_fmt yuv420p " + prefix + ".mp4"]
            print("Creating video file in " + str(_dir.absolute()) + " with\n  " + cmd[0])
            VolcaniteExec.run_log(cmd, cwd=str(_dir.absolute()), shell=True)


if __name__ == "__main__":
    # setup the evaluation output directory and the log files
    evaluation = VolcaniteEvaluation("./my_test_eval", ExistingPolicy.DELETE, "my_test_eval",
                                     [VolcaniteLogFileCfg("results.txt",
                                                                  fmts=["{name},{comprate_pcnt:.3},{frame_avg_ms}"],
                                                                  headers=["Name,Compression Rate [%],frame avg [ms]"])],
                                     enable_log=True, dry_run=False)

    volcanite = VolcaniteExec(evaluation, None, "cmake-build-release")
    volcanite.checkout_and_build()

    # print evaluation information to console
    print(volcanite.info_str())
    print('\n'.join(volcanite.logs_info_str()))

    # iterate over all configuration combinations and execute Volcanite
    for arg_data in VolcaniteArg.args_datasynth.values():
        vargs = [arg_data, VolcaniteArg.arg_image_export([arg_data])]
        name = VolcaniteArg.concat_ids(vargs)

        # manually log a comment line to the log file
        evaluation.get_log().log_manual("#Running evaluation: " + name)
        # execute Volcanite and pass the Volcanite log file
        volcanite.exec(vargs, name)

    # create a copy of the log file without comment lines that start with # which includes the #fmt: strings
    evaluation.get_log().create_formatted_copy(evaluation.eval_out_directory / Path("results.csv"),
                                               remove_line_prefixes=["#"])

