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
    evaluation. It may use the {name} placeholder for the name of the current evaluation.
    """

    def __create_fallback_string(self, replace_with: str = "") -> str | None:
        """Reads the format string from the log file and replaces all placeholders (anything within {...} curly braces) with replace_with."""

        format_string = '\n'.join(self.__fmt_strs)

        # replace any {...} except {name} which is available in the fallback string
        pattern = r'\{(?!name\}).*?\}'
        format_string = re.sub(pattern, lambda m: replace_with, format_string)

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
        self.file_path: Path = log_file
        self.__fmt_strs = fmt_strs
        self.__header_strings = header_strings

        if use_fmt_from_existing_log and self.file_path.exists():
            self.__fmt_strs, _ = VolcaniteLogFile.get_fmt_and_remainder_lines_from_file(self.file_path)
        self.fallback_log: str = fallback_log_line if fallback_log_line is not None else self.__create_fallback_string()
        self.disable_manual_logs = False

    @classmethod
    def create_from_template_log_file(cls, log_file: Path, template_log_file: Path,
                                      fallback_log_line: str | None, use_fmt_from_existing_log: bool = True) -> Self:
        format_strings, header_strings = cls.get_fmt_and_remainder_lines_from_file(template_log_file)
        return cls(log_file=log_file, fmt_strs=format_strings, header_strings=header_strings,
                   fallback_log_line=fallback_log_line, use_fmt_from_existing_log=use_fmt_from_existing_log)

    def get_log_file(self) -> Path:
        return self.file_path

    def get_fmt_and_header_lines(self) -> tuple[list[str], list[str]]:
        return self.__fmt_strs, self.__header_strings

    def setup(self, old_log_policy: ExistingPolicy = ExistingPolicy.ABORT):
        """
        Ensures that the log_file exists at its location.
        :param old_log_policy: handling of existing log files, either 'abort' (default), 'append', or 'overwrite'
        """

        if self.file_path.exists():
            if old_log_policy == ExistingPolicy.ABORT:
                raise IOError("Log file " + str(self.file_path) + " exist and existing policy is 'abort'")
            elif old_log_policy == ExistingPolicy.MOVE:
                shutil.move(self.file_path, str(self.file_path.resolve()) + "_" + datetime.now().strftime("%Y%m%d-%H%M%S"))
            elif old_log_policy == ExistingPolicy.DELETE:
                self.file_path.unlink()

            if old_log_policy != ExistingPolicy.APPEND:
                with(open(self.file_path, "w")) as f:
                    f.writelines(self.__fmt_strs)
                    f.writelines(self.__header_strings)
        else:
            with(open(self.file_path, "w")) as f:
                f.writelines("#fmt:" + line + "\n" for line in self.__fmt_strs)
                f.writelines(line + "\n" for line in self.__header_strings)

        if not self.file_path.exists():
            raise IOError(f"Could not create log file {self.file_path}")

    def log_manual(self, output: str, end: str = "\n") -> None:
        if self.disable_manual_logs:
            return
        with open(str(self.file_path), "a") as log_out:
            log_out.write(output + end)

    def create_formatted_copy(self, dest: Path, newline_separator: str = None, remove_line_prefixes: list[str] = None,
                              replace_map: dict[str, str] = None):
        """
        Copies the current log file to dest and re-formats the file:
        Removes all lines starting with one of the remove_line_prefixes if given.
        Removes existing line breaks and creates new line breaks at any occurring newline_separator if it is given.
        Uses the replace_map to replace any key with its value if it is given.
        """
        if not self.file_path.exists():
            raise FileNotFoundError(f"Log file {self.file_path} does not exist")
        with open(self.file_path, 'r') as log_in:
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
            print(f"create formated copy of {self.file_path} to {dest}")

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
            VolcaniteArg.setup_directories(veval=self,
                                           git_base_directory=VolcaniteArg.get_git_directory(),
                                           csgv_directory=VolcaniteArg.get_csgv_directory(),
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
    args_csgv_datasets: dict[str, Self] = {}

    __csgv_directory: Path = None
    __vcfg_directory: Path = None
    __eval_directory: Path = None
    __git_directory: Path = None

    @classmethod
    def get_path_setup_filename(cls) -> Path:
        """
        Name of a setup file from which all evaluation scripts will automatically read relevant paths.
        The file must contain the following newline terminated lines:
        config_dir: [directory storing one {name}.vcfg file per {name}.csgv data sets]
        csgv_dir: [directory storing previously compressed {name}.csgv data sets]
        volcanite_src: [base directory of the cloned Volcanite git repository]
        """
        return Path("volcanite-eval-setup.txt")

    @classmethod
    def setup_directories(cls, veval: VolcaniteEvaluation | None = None, git_base_directory: PathLike | None = None,
                          csgv_directory: PathLike | None = None, vcfg_directory: PathLike | None = None):
        """
        Sets static paths to directories that are referenced when creating certain VolcaniteArgs.
        If csgv_directory or vcfg_directory are None, it is attempted to set them up from a previously created
        path setup file (with the name returned by get_path_setup_filename()) in the current working directory.
        :param git_base_directory: base directory of the cloned Volcanite git repository
        :param csgv_directory: directory where newly compressed CSGV files are exported to and imported from
        :param vcfg_directory: directory containing config and rec files for the argument sets
        :param veval: the Volcanite evaluation specifying the evaluation output directory for images and videos
        """
        if not veval:
            raise ValueError("VolcaniteEvaluation must not be None")

        cls.__csgv_directory = Path(csgv_directory) if csgv_directory else None
        cls.__vcfg_directory = Path(vcfg_directory) if vcfg_directory else None
        cls.__git_directory = Path(git_base_directory) if git_base_directory else None
        cls.__eval_directory = veval.eval_out_directory if veval else None

        if not cls.__git_directory:
            cls.__git_directory = Path(subp.Popen(["git", "rev-parse", "--show-toplevel"], stdout=subp.PIPE).communicate()[0].rstrip().decode('utf-8'))
            print(f"obtained volcanite git base directory {cls.__git_directory} with 'git rev-parse --show-toplevel'")

        # auto initialize non-passed paths from possible evaluation setup file in working directory
        if not csgv_directory or not vcfg_directory or not git_base_directory:
            setup_file = cls.get_path_setup_filename()
            print(setup_file)
            if not setup_file.exists() and cls.__git_directory:
                setup_file = cls.__git_directory / "eval" / cls.get_path_setup_filename()
                print(setup_file)
            if setup_file.exists():
                print(f"setup paths from {setup_file}")
                with open(setup_file, 'r') as file:
                    for l in file.readlines():
                        parts = l.split(":", 1)
                        if len(parts) != 2:
                            continue
                        if parts[0] == "csgv_dir" and not csgv_directory:
                            cls.__csgv_directory = Path(parts[1].strip())
                        if parts[0] == "config_dir" and not vcfg_directory:
                            cls.__vcfg_directory = Path(parts[1].strip())
                        if parts[0] == "volcanite_src" and not git_base_directory:
                            cls.__git_directory = Path(parts[1].strip())
            else:
                print(f"unable to setup paths. missing {cls.get_path_setup_filename()} file.")


        if cls.__csgv_directory and not cls.__csgv_directory.exists():
            raise FileNotFoundError(f"CSGV directory {cls.__csgv_directory} not found")
        if cls.__vcfg_directory and not cls.__vcfg_directory.exists():
            raise FileNotFoundError(f"vcfg config directory {cls.__vcfg_directory} not found")
        if cls.__eval_directory and not cls.__eval_directory.exists():
            raise FileNotFoundError(f"Evaluation output directory {cls.__eval_directory} not found")
        if cls.__git_directory:
            if not cls.__git_directory.exists():
                raise FileNotFoundError(f"Volcanite git directory {cls.__git_directory} not found")
            elif cls.__git_directory.name != "volcanite":
                print(f"Warning: expected Volcanite git directory to be named volcanite but is {cls.__git_directory.name}")

        # setup list of .csgv data sets from directory
        if cls.__csgv_directory:
            for file in cls.__csgv_directory.glob('*.csgv'):
                cls.args_csgv_datasets[file.stem] = cls.arg_csgv_import(name=file.stem)
        

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
    def get_git_directory(cls):
        return cls.__git_directory


    def __init__(self, args: list[str] | str, identifier: str | None = None, priority: float | None = None):
        """
        Encapsulates a Volcanite command line argument.
        If no identifier or priority are given, this argument will not be included in concat_id identifier strings.

        :param args: list or string of space separated arguments passed to the Volcanite call
        :param identifier: short identifier of the argument used to form evaluation name strings
        :param prio: priority to sort the identifiers in the evaluation name string
        """
        if isinstance(args, str):
            self.args = args.split(' ')
        else:
            self.args = args

        self.identifier = "" if identifier is None else identifier
        self.prio = 1000 if priority is None else priority


    @classmethod 
    def concat_arg_string(cls, args: list[Self] | None) -> str:
        """Create the combined plain argument string passed for all passed args sorted by their priority."""
        if not args:
            return ""
        
        sorted_by_prio = sorted(args, key=lambda a: a.prio)
        return " ".join([" ".join(a.args) for a in sorted_by_prio])

    @classmethod
    def concat_ids(cls, args: list[Self] | None) -> str:
        """Create a concatenated string identifier for all passed args sorted by their priority."""
        if not args:
            return ""
        
        sorted_by_prio = sorted(args, key=lambda a: a.prio)
        return "_".join([a.identifier for a in sorted_by_prio if a.identifier])

    @classmethod
    def arg_csgv_export(cls, args: list[Self]) -> Self:
        if cls.__csgv_directory is None:
            raise RuntimeError("VolcaniteArg static csgv directory must be initialized before usage"
                               "(VolcaniteArg.set_directories)")
        return cls(["-c", str(cls.__csgv_directory) + "/" + cls.concat_ids(args) + ".csgv"])

    @classmethod
    def arg_csgv_import(cls, name: str | None = None, args: list[Self] | None = None) -> Self:
        """
        Returns args for importing a previously compressed csgv file from the csgv directory.
        The csgv file will be assumed to be {name}{concatenated arg ids}.csgv in that directory.
        The arg id will be {name} with priority 0.
        """
        if cls.__csgv_directory is None:
            raise RuntimeError("VolcaniteArg static csgv directory must be initialized before usage"
                               "(VolcaniteArg.set_directories)")
        
        if not name and (not args or args.empty()):
            raise RuntimeError("Must provide at least one of name or args when creating .csgv import VolcaniteArg.")
        
        return cls([str(cls.__csgv_directory) + "/" + name + cls.concat_ids(args) + ".csgv"], name, 0)

    @classmethod
    def arg_image_export(cls, args: list[Self], filetype: str = "png") -> Self:
        if cls.__eval_directory is None:
            raise RuntimeError("VolcaniteArg static evaluation directory must be initialized before usage"
                               "(VolcaniteArg.set_directories)")
        return cls(["-i", str(cls.__eval_directory) + "/" + cls.concat_ids(args) + "." + filetype], "", 110)
    
    @classmethod
    def arg_image_eval_cfg(cls, sample_count: int = 1024):
        """
        Returns an argument to set the evaluation rendering configuration to a still perspective
        (e.g. set by arg_config_import). Cannot be used in combination with arg_video_cfg.

        :param sample_count: how many frames are rendered in the still perspective.
        """

        if sample_count <= 0:
            raise RuntimeError("Sample count for arg_image_eval_cfg must be > 0.")

        return cls(["--video-cfg", f"d1s{sample_count}r0:0z0:0", "--config", f"[Display] Accumulation Frames: {sample_count}"], "", 111)

    
    @classmethod
    def arg_rec_import(cls, args: list[Self]) -> Self:
        if cls.__vcfg_directory is None:
            raise RuntimeError("VolcaniteArg static vcfg directory must be initialized before usage"
                               "(VolcaniteArg.set_directories)")
        return cls(["--record-in", str(cls.__vcfg_directory / cls.concat_ids(args) + ".rec")])

    @classmethod
    def arg_video_eval_cfg(cls, rotation=(-360, 0), zoom=(2, 0), duration=600, duration_is_seconds=False,
                      output_framerate=30, interpolant : str = "smooth", edge=(0.2, 0.8)):
        """
        Creates a VolcaniteArg for animating video rendering camera paths.
        This path will be used for evaluations and video export.
        Cannot be used in combination with arg_image_eval_cfg().

        :param rotation: min/max camera rotation in degrees.
        :param zoom: min/max camera zoom.
        :param duration: either number of frames or number of seconds to render.
        :param duration_is_seconds: if the duration parameter is in seconds instead of frame count.
        :param output_framerate: either the frame rate of the encoded video file or 0 to use real frame durations.
        """
        if output_framerate < 0:
            raise RuntimeError("Video encoding frame rate must be >= 0")
        
        # other cfg parameters: interpolant, edge
        if interpolant.lower() == "smooth":
            interpol_str = "i1"
        elif interpolant.lower() == "smoother":
            interpol_str = "i2"
        else:
            interpol_str = "i0"
        return cls(["--video-cfg", f"d{-duration if duration_is_seconds else duration}r{rotation[0]}:{rotation[1]}z{zoom[0]}:{zoom[1]}o{output_framerate}{interpol_str}e{edge[0]}:{edge[1]}"])

    @classmethod
    def arg_video_export(cls, args) -> Self:
        if cls.__eval_directory is None:
            raise RuntimeError("VolcaniteArg static evaluation directory must be initialized before usage"
                               "(VolcaniteArg.set_directories)")
        video_dir = (Path(cls.__eval_directory) / cls.concat_ids(args)).absolute()
        video_dir.mkdir(parents=True, exist_ok=True)
        return cls(["-v", str(video_dir) + "/" + cls.concat_ids(args) + "_{:04}.jpg"])

    @classmethod
    def arg_config_import(cls, eval_config_file: list[Self] | str | None, additional_configs: list[str] | str | None = None, resolution: str = "1920x1080") -> Self:
        if eval_config_file and cls.__vcfg_directory is None:
            raise RuntimeError("VolcaniteArg static vcfg directory must be initialized before usage"
                               "(VolcaniteArg.set_directories)")
        
        args_config_file = []
        if eval_config_file:
            if not isinstance(eval_config_file, str):
                eval_config_file = cls.concat_ids(eval_config_file) + ".vcfg"
            elif not eval_config_file.endswith(".vcfg"):
                eval_config_file += ".vcfg"
            
            eval_config_file = str(cls.__vcfg_directory / eval_config_file)
            args_config_file += ["--config", eval_config_file]

        # example additional_configs: ["path-tracing", "[Display] Accumulation Frames: 8"]
        # should become: ["path-tracing,"\"[Display]", "Accumulation", "Frames:", "8\""]
        args_config_str = []
        if additional_configs:
            if isinstance(additional_configs, str):
                additional_configs = [additional_configs]

            for cfg in additional_configs:
                cfg = cfg.strip()
                if not cfg:
                    continue
                args_config_str += ["--config", cfg]

        print("\n\n" + str(args_config_file + args_config_str) + "\n\n")

        return cls(args_config_file + args_config_str + ["--resolution", resolution], "", 100)

    @classmethod
    def arg_dataset(cls, data_path: str, identifier: str | None = None,
                    chunks: tuple[int, int, int] | None = None):
        """
        Creates a VolcaniteArg for loading the data set (.csgv or non-.csgv) located at data_path.
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
VolcaniteArg.args_encoding = {"nibble": VolcaniteArg(["-s", "0"], "nb", 1),
                              "nibble_ra": VolcaniteArg(["-s", "0", "-p", "-o", "pnls"], "nb-ra", 1),
                              "rANS1": VolcaniteArg(["-s", "1"], "rans1", 1),
                              "rANS": VolcaniteArg(["-s", "2"], "rans", 1),
                              "wmh_nosb": VolcaniteArg(["-s", "2", "-p", "-o", "pnl", "p"], "wm-sb", 1),
                              "wmh": VolcaniteArg(["-s", "2", "-p", "-o" ,"pnls"], "wm-sb", 1)}
VolcaniteArg.args_brick_size = {"16": VolcaniteArg(["-b", "16"], "b16", 2),
                                "32": VolcaniteArg(["-b", "32"], "b32", 2),
                                "64": VolcaniteArg(["-b", "64"], "b64", 2)}
VolcaniteArg.args_cache_mode = {"none": VolcaniteArg(["--cache-mode", "n"], "csh-n", 3),
                            "voxel": VolcaniteArg(["--cache-mode", "v", "--empty-space-res", "0"], "csh-v", 3),
                            "voxel_es": VolcaniteArg(["--cache-mode", "v", "--empty-space-res", "2"], "csh-v_es", 3),
                            "brick": VolcaniteArg(["--cache-mode", "b"], "csh-b", 3),
                            "brick_sm": VolcaniteArg(["--cache-mode", "b", "--decode-sm"], "csh-bsm", 3)}
VolcaniteArg.args_shading = {"local": VolcaniteArg(["--config", "local-shading"], "local", 105),
                             "shadow": VolcaniteArg(["--config", "global-shadows"], "shadow", 105),
                             "ao": VolcaniteArg(["--config", "ambient-occlusion"], "ao", 105),
                             "pt": VolcaniteArg(["--config", "path-tracing"], "pt", 105)}
VolcaniteArg.args_datasynth = {"dSynth8": VolcaniteArg(["+synth_1024x1024x1024_r6x6x6-10x10x10"], "dSynth8", 0),
                            "dSynth32": VolcaniteArg(["+synth_1024x1024x1024_r24x24x24-40x40x40"], "dSynth32", 0),
                            "dSynth128": VolcaniteArg(["+synth_1024x1024x1024_r96x96x96-160x160x160"], "dSynth128", 0),
                            "dSynth512": VolcaniteArg(["+synth_1024x1024x1024_r384x384x384-640x640x640"], "dSynth512", 0)}
VolcaniteArg.args_default = {"verbose": VolcaniteArg(["--verbose"]),
                             "headless": VolcaniteArg(["--headless"])}

class VolcaniteExec:
    """
    Interface for compiling and executing Volcanite.
    :var evaluation: the VolcaniteEvaluation including the evaluation output directory, name and all log files
    :var git_checkout: git commit or branch that is pulled and build before the first execution of Volcanite
    :var build_subdir: the build sub-directory in the specified Volcanite source directory
    """

    @classmethod
    def __run_process(cls, call_args: list[str] | str, cwd: str | PathLike | None = None, print_log=True, *args, **kwargs):
        # remove empty argument strings ""
        if isinstance(call_args, str):
            call_args = call_args.strip().split()

        call_args = list(filter(None, call_args))
        if print_log:
            print(str(cwd) + "> " + " ".join(call_args))
        return subp.run(call_args, cwd=cwd, *args, **kwargs)

    @classmethod
    def build_volcanite(cls, build_dir: Path) -> Path:
        """Builds Volcanite inside the given build_dir. If the build_dir name contains 'deb' or 'debug', a debug build
        is done. Otherwise Volcanite is build in Release mode. Building requires cmake and all build dependencies.
        :returns: a Path to the directory containing the ./volcanite binary"""
        if not build_dir.exists():
            build_dir.mkdir(parents=True, exist_ok=True)
            build_type = "-DCMAKE_BUILD_TYPE=Debug" if "deb" in str(build_dir.stem).lower() else "-DCMAKE_BUILD_TYPE=Release"
            res = cls.__run_process(["cmake", build_type, ".."], cwd=build_dir)
            if res.returncode != 0:
                raise RuntimeError(f"Error: cmake returned {res.returncode}")

        res = cls.__run_process(["cmake", "--build", ".", "-j", "--target", "volcanite"], cwd=build_dir)
        if res.returncode != 0:
            raise RuntimeError(f"Error: building target volcanite returned {res.returncode}")
        return build_dir / "volcanite"

    @classmethod
    def run_volcanite(cls, binary_dir: str | PathLike, args: list[str] | str, print_log: bool = True):
        """Executes Volcanite with args as argument string and no special evaluation log file handling."""
        if isinstance(args, str):
            return VolcaniteExec.__run_process("./volcanite " + args.strip(), print_log=print_log, cwd=binary_dir)
        else:
            return VolcaniteExec.__run_process(["volcanite"] + args, print_log=print_log, cwd=binary_dir)

    def __init__(self, evaluation: VolcaniteEvaluation, git_base_dir: str | PathLike | None = None,
                 git_checkout : str | None = None, build_subdir: str | PathLike = "cmake-build-release",
                 binary_dir: str | PathLike | None = None):
        """
        Creates a Volcanite executor for the given evaluation. If no binary_dir is provided,

        :param evaluation: the VolcaniteEvaluation including the evaluation output directory, name and all log files
        :param git_base_dir: the base directory of the Volcanite git repository
        :param git_checkout: git commit, tag, or branch name that is checked out before building Volcanite
        :param build_subdir: directory in the git repository in which Volcanite is build (default: cmake-build-release)
        :param binary_dir: directory in which the ./volcanite binary is located
        """
        # build directory must have a depth of one
        if len(Path(build_subdir).parents) != 1:
            raise ValueError("Volcanite build sub-directory must have a path depth of one")

        self.evaluation = evaluation
        self.git_checkout = git_checkout
        if binary_dir:
            self.binary_dir = Path(binary_dir)
            if git_base_dir:
                raise ValueError("Can either provide git_base_dir or binary_dir but not both")
            self.git_base_dir = None
            self.build_dir = None
            self.__is_build = True
            if (not (self.binary_dir / "volcanite").exists()
                    or not (self.binary_dir / "volcanite").is_file()):
                raise ValueError("Volcanite binary_dir does not contain a ./volcanite executable")
        else:
            if git_base_dir:
                self.git_base_dir = Path(git_base_dir)
            else:
                if VolcaniteArg.get_git_directory():
                    self.git_base_dir = VolcaniteArg.get_git_directory()
                else:
                    self.git_base_dir = Path(subp.Popen(["git", "rev-parse", "--show-toplevel"], stdout=subp.PIPE).communicate()[0].rstrip().decode('utf-8'))
                    print(f"obtained volcanite git base directory {self.git_base_dir} with 'git rev-parse --show-toplevel'")
            if self.git_base_dir.name != "volcanite":
                print(f"Warning: expected git base directory to be named volcanite but is {self.git_base_dir.name}")
                self.git_base_dir = None
            self.build_dir = self.git_base_dir / build_subdir
            self.binary_dir = self.build_dir / "volcanite"    # volcanite executable is in the volcanite subdir
            self.__is_build = False

    def info_str(self):
        result: str = f"{datetime.now().strftime("%Y.%m.%d-%H:%M:%S")} [{self.evaluation.name}]"
        result +=  f" exe:{"off" if self.evaluation.dry_run else "on"} log:{"on" if self.evaluation.log_files else "off"}"
        if not self.git_base_dir:
            result += f" build {str(self.git_checkout) + "@" if self.git_checkout else ""}{self.build_dir},"
        result += f" executable {str(self.binary_dir)}/volcanite"
        return result

    def logs_info_str(self):
        log_strs = []
        for l in self.evaluation.log_files:
            log_strs.append(f"{l.get_log_file()} [{' '.join(l.get_fmt_and_header_lines()[0])}] fallback: '{l.fallback_log}'")
        return log_strs

    def checkout_and_build(self):
        """Checks out the git commit (if configured) and builds volcanite into the configured build sub-directory."""

        if self.evaluation.dry_run:
            print("Skipping Volcanite build in dry run")
            return
        elif not self.git_base_dir:
            print("Skipping Volcanite build as no volcanite git directory is given")
            return

        if self.git_checkout:
            VolcaniteExec.__run_process(["git", "checkout", self.git_checkout], cwd=self.git_base_dir)
            res = VolcaniteExec.__run_process(["git", "pull"], cwd=self.git_base_dir)
            if res.returncode != 0:
                raise RuntimeError(f"Error: git pull returned {res.returncode}")

        VolcaniteExec.build_volcanite(build_dir=self.build_dir)
        self.__is_build = True

    def exec(self, args : list[VolcaniteArg], eval_name: str = None, headless: bool = True):
        """
        Executes Volcanite with the specified arguments. Volcanite is compiled first if it was not build yet.
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
            exec_call_args += ["--eval-logfiles", str(','.join([str(log.file_path.resolve())
                                                                for log in self.evaluation.log_files]))]
            if eval_name:
                exec_call_args += ["--eval-name", eval_name]

        # append all user passed arguments that are encapsulated in VolcaniteArg objects, sorted by priority
        # args example:   [VolcaniteArg(["-b", "16"], "b16"), VolcaniteArg(["-s", "0"], "nb")]
        args.sort(key=lambda a: a.prio)
        exec_call_args = exec_call_args + [a for volcanite_arg in args for a in volcanite_arg.args]
        print("RUN VOLCANITE -----------------  " + eval_name)
        print(" ".join(exec_call_args))
        print("-------------------------------")
        if not self.evaluation.dry_run:
            res = VolcaniteExec.__run_process(exec_call_args, print_log=False, cwd=self.binary_dir)
            if res.returncode != 0:
                print("Error: volcanite returned " + str(res.returncode))
                if self.evaluation.enable_log:
                    for log in self.evaluation.log_files:
                        if log.fallback_log:
                            print("Error: Volcanite returned " + str(res.returncode))
                            log.log_manual(log.fallback_log.replace("{name}", eval_name) + "\n")
                        else:
                            raise RuntimeError(f"Volcanite returned {res.returncode} and no fallback log exists for {log.file_path}")
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
            VolcaniteExec.__run_process(cmd, cwd=str(_dir.absolute()), shell=True)

