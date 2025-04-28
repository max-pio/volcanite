//  Copyright (C) 2024, Max Piochowiak, Karlsruhe Institute of Technology
//
//  This program is free software: you can redistribute it and/or modify
//  it under the terms of the GNU General Public License as published by
//  the Free Software Foundation, either version 3 of the License, or
//  (at your option) any later version.
//
//  This program is distributed in the hope that it will be useful,
//  but WITHOUT ANY WARRANTY; without even the implied warranty of
//  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//  GNU General Public License for more details.
//
//  You should have received a copy of the GNU General Public License
//  along with this program.  If not, see <https://www.gnu.org/licenses/>.

#pragma once

#include <tclap/CmdLine.h>
#ifndef HEADLESS
#include "portable-file-dialogs.h"
#endif

#include "CSGVPathUtils.hpp"
#include "csgv_constants.incl"
#include "volcanite/util/segmentation_volume_synthesis.hpp"
#include "vvv/util/Logger.hpp"

#include <fmt/core.h>
#include <optional>
#include <ranges>
#include <sstream>
#include <string>

using namespace vvv;

namespace volcanite {

struct VolcaniteArgs {

  public:
    // general args
    bool verbose = false;
    bool headless = false;
    std::string input_file;                 ///< must be .csgv if compress is false, otherwise vti / raw / hdf5
    bool chunked = false;                   ///< if the first 3 {} in the input string should be chunk ids formatted
    uint32_t chunk_files[3] = {0u, 0u, 0u}; ///< max. xyz index of chunk files. e.g. (1,3,0) would load 8 chunk files
    uint32_t threads = 0;                   ///< number of CPU threads (0 = system supported concurrent threads)
    std::filesystem::path working_dir;      ///< working directory, usually contains the .csgv. Maybe a temp directory.

    // rendering args
    std::vector<std::string> rendering_configs; ///< one or more .vcfg files (ends with .vcfg) or config strings
    uint32_t render_resolution[2] = {1920, 1080};
    bool fullscreen = false;
    bool stream_lod = false;
    size_t cache_size_MB = 1024ul;
    uint32_t cache_mode = CACHE_BRICKS;
    bool cache_palettized = false;
    bool decode_from_shared_memory = false;
    uint32_t empty_space_resolution = 0u; ///< in cache mode CACHE_VOXELS, groups n³ voxels into one empty space entry
    bool show_development_gui = false;
    bool enable_vsync = true;

    // attribute args
    std::string attribute_database;            ///< SQlite3 file with attributes for volume labels
    std::string attribute_table;               ///< table or view containing the attributes for the volume labels
    std::string attribute_label;               ///< name of the label attribute
    std::string attribute_csv_separator = ","; //< only for csv attribute databases
    bool label_remapping = false;              ///< if label ids in the volume should be remapped to a consecutive interval

    // compression args
    std::string compress_export_file;   ///< !empty = perform compression to file         Only one of
    std::string decompress_export_file; ///< !empty = perform decompression to file       both can be set!
    std::string segmented_volume_file;
    uint32_t brick_size = 32;
    EncodingMode encoding_mode = EncodingMode::DOUBLE_TABLE_RANS_ENC;
    uint32_t freq_subsampling = 8;                  ///< n^3 factor for subsampling bricks for frequency table computation with rANS
    uint32_t operation_mask = OP_ALL_WITHOUT_DELTA; // enables certain CSGV operations and stop bits through OP_*_BIT
    bool random_access = false;                     ///< encode bricks so that they support random access within a brick

    // evaluation and statistics
    std::string screenshot_output_file; ///< png or jpg output file path to export the last frame from headless rendering
    std::string video_output_fmt_file;  ///< output image file path string accepted by std::format for immediate frames
    bool run_tests = false;
    bool export_stats = false;
    std::string record_in_file = {};             ///< file that stores a previously exported camera path for replay in headless
    uint32_t record_convergence_frames = 1;      ///< number of render frames that are accumulated per output frame of a camera path
    std::vector<std::string> eval_logfiles = {}; // files into which evaluation results are exported (with 'append')
    std::string eval_name = {};                  ///< name of the evaluation run that can be accessed in the log file as "{name}"
    bool print_eval_keys = false;                ///< if true, prints all available evaluation log keys to the console on startup
    std::string shader_defines = {};             ///< string of shader defines that will be passed on to the shader compiler

    static std::string getHelpString() {
        std::stringstream ss;
        ss << "EXAMPLES:" << std::endl;
        ss << "./volcanite --headless -r 1920x1080 -i screenshot.png volume.vti" << std::endl
           << "\tExports a render image without starting the application." << std::endl;
        ss << "./volcanite --headless -b 64 -s 2 -c out.csgv volume.vti" << std::endl
           << "\tExports a strongly compressed volume." << std::endl;
        ss << "./volcanite --headless -d out.vti volume.csgv" << std::endl
           << "\tDecompresses volume.csgv to out.vti." << std::endl;
        ss << "./volcanite volume" << std::endl
           << "\tStarts the Volcanite renderer for the given volume." << std::endl;
        ss << "./volcanite --config local-shading --cache-size 512 -b 32 -s 2 --freq-sampling 8 --stream-lod volume.vti" << std::endl
           << "\tStarts Volcanite for limited GPU capabilities." << std::endl;
        ss << "./volcanite --headless -c out.csgv --chunked 1,3,0 vol_x{}_y{]_z{}.vti" << std::endl
           << "\tCompresses chunked volume vol_x0_y0_z0.vti to vol_x1_y3_z0.vti." << std::endl;
        return ss.str();
    };

    static std::string getVolcaniteVersionString() {
        return VOLCANITE_VERSION;
    }

    bool performCompression() const {
        return !input_file.ends_with(".csgv");
    }

    bool performDecompression() const {
        return !decompress_export_file.empty();
    }

    static std::optional<VolcaniteArgs> parseArguments(int argc, char *argv[], bool input_volume_required = true) {
        VolcaniteArgs va;

        using namespace TCLAP;
        try {
            CmdLine cmd(getHelpString(), ' ', getVolcaniteVersionString());

            // could include TCLAP grouping here using AnyOf, EitherOf

            // compression arguments
            ValueArg<std::string> decompresspathArg("d", "decompress", "Export the decompressed volume to given file.", false, va.decompress_export_file, "file", cmd);
            ValueArg<std::string> compresspathArg("c", "compress", "Export the compressed volume to the given csgv file and any attribute database along with it.", false, va.compress_export_file, "file", cmd);
            ValueArg<std::string> chunkedArg("", "chunked", "Compress chunked segmented volume using formatted <volume> path with inclusive x, y, and z chunk file ranges as: \".*{[0..<xn>]}.*{[0..<yn>]}.*{[0..<zn>]}.*\".", false, "", "xn,yn,zn", cmd);
            ValueArg<uint32_t> subsamplingArg("", "freq-sampling", "Compression prepass acceleration by given factor cubed. Affects strength 1 or 2 only.", false, va.freq_subsampling, "int", cmd);
            ValueArg<uint32_t> threadsArg("", "threads", "Number of CPU threads for (de)compression parallelization.", false, va.threads, "int", cmd);
            std::vector<uint32_t> _allowedStrength = {0u, 1u, 2u};
            ValuesConstraint<uint32_t> allowedStrength(_allowedStrength);
            ValueArg<uint32_t> strengthArg("s", "strength", "Compress with more expensive but stronger variable bit-length encoding (1). Use two frequency tables for even stronger compression (2).", false, 2, &allowedStrength);
            cmd.add(strengthArg);
            std::vector<uint32_t> _allowedBrickSize = {8u, 16u, 32u, 64u, 128u};
            ValuesConstraint<uint32_t> allowedBrickSize(_allowedBrickSize);
            ValueArg<uint32_t> bricksizeArg("b", "brick-size", "Compress with given brick size.", false, va.brick_size, &allowedBrickSize);
            cmd.add(bricksizeArg);
            ValueArg<std::string> opMaskArg("o", "operations", "Combination of [p]arent, all [n]eighbors / [x,y,z] neighbor, palette [l]ast, palette [d]elta, [s]top bits. Quick: [a]ll or [o]ptimized.", false, "o", "(a|o|p|n|x|y|z|l|d[-]|s)*", cmd);
            SwitchArg randomAccessArg("", "random-access", "Encode in a format that supports random access and in-brick parallelism for the decompression.", cmd);
            // evaluation and statistics arguments
            SwitchArg testArg("t", "test", "Run test after performing the compression", cmd);
            SwitchArg statsArg("", "stats", "Export statistics after performing the compression", cmd);
            ValueArg<std::string> recordInFileArg("", "record-in", "File that stores a previously exported camera path for replay on startup. Must be used with -i or -v.", false, va.record_in_file, "file", cmd);
            ValueArg<uint32_t> recordConvergenceArg("", "record-frames", "How many render frames are accumulated per output frame of a camera path. Must be used with --record-in or -v.", false, va.record_convergence_frames, "int", cmd);
            ValueArg<std::string> evalLogFilesArg("", "eval-logfiles", "Comma separated files into which evaluation results are appended.", false, "", "file", cmd);
            ValueArg<std::string> evalNameArg("", "eval-name", "Title of this evaluation which will be available in log files as \"{name}\". Must be used with --eval-logfile.", false, va.eval_name, "string", cmd);
            SwitchArg evalPrintArg("", "eval-print-keys", "Print all available evaluation keys to the console and exit.", cmd);
            ValueArg<std::string> shaderDefineArg("", "shader-def", "String of ; separated definitions that will be passed on to the shader. e.g. 'MY_VAL=64;MY_DEF'. Use with care.", false, va.shader_defines, "string", cmd);

            // attribute arguments
            SwitchArg labelRemappingArg("", "relabel", "Relabel the voxel labels even if no attribute database is used.", cmd);
            ValueArg<std::string> attributeArg("a", "attribute", R"(SQLite or CSV Attribute database: "{file.sqlite}[,{table/view name}[,{label column referenced in volume}]]" or "{file.csv}[,{label column referenced in volume}[,{csv separator}]]".)", false, "", "database.sqlite[,table[,label]] or database.csv[,label[,separator]]", cmd);
            // rendering arguments
            SwitchArg devArg("", "dev", "Reveal development GUI and enable shader debug outputs.", cmd);
            SwitchArg noVsyncArg("", "no-vsync", "Disable vertical synchronization in renderer.", cmd);
            ValueArg<uint32_t> cacheSizeMBArg("", "cache-size", "Size in MB of the renderer's brick cache. 0 to allocate all available.", false, va.cache_size_MB, "size", cmd);
            SwitchArg cachePalettizedArg("", "cache-palette", "Store palette indices in brick cache instead of labels.", cmd);
            std::vector<char> _allowedCacheUnits = {'n', 'v', 'b'};
            ValuesConstraint<char> allowedCacheUnits(_allowedCacheUnits);
            ValueArg<char> cacheModeArg("", "cache-mode", "Content in the cache: [n] no cache [v] single voxels [b] full bricks (default)", false, _allowedCacheUnits[va.cache_mode], &allowedCacheUnits);
            cmd.add(cacheModeArg);
            SwitchArg decodedSharedMemoryArg("", "decode-sm", "Copy brick encodings to shared memory before decoding.", cmd);
            std::vector<uint32_t> _allowedESSRes = {0, 1, 2, 4, 8, 16, 32, 64};
            ValuesConstraint<uint32_t> allowedESSRes(_allowedESSRes);
            ValueArg<uint32_t> emptySpaceResolutionArg("", "empty-space-res", "Groups n³ voxels into one empty space entry. Requires cache-mode v. Set 0 to disable empty space skipping.", false, va.empty_space_resolution, &allowedESSRes);
            cmd.add(emptySpaceResolutionArg);
            SwitchArg streamlodArg("", "stream-lod", "Stream finest level of detail to GPU on demand. Helps with low GPU memory.", cmd);
            ValueArg<std::string> imageArg("i", "image", "Renders an image to the given file on startup.", false, va.screenshot_output_file, "file", cmd);
            ValueArg<std::string> videoArg("v", "video", "Video output with one image output file per frame. The formatted file path must contain a single {} placeholder which will be replaced with frame index. Example: ./out{:04}.jpg", false, va.video_output_fmt_file, "formatted file", cmd);
            ValueArg<std::string> resolutionArg("r", "resolution", "Startup render resolution as [Width]x[Height].", false, "", "[Width]x[Height]", cmd);
            SwitchArg fullscreenArg("", "fullscreen", "Start renderer in fullscreen mode.", cmd);
            ValueArg<std::string> renderconfigArg("", "config", "List of .vcfg files, rendering presets, or direct config strings '[{GUI window}] {parameter label}: {parameter value(s)}', separated by ;", false, "", "{(.vcfg file | rendering preset | string);}*", cmd);
            // general arguments
            SwitchArg headlessArg("", "headless", "Do not start GUI application.", cmd);
            SwitchArg verboseArg("", "verbose", "Verbose debug output.", cmd);

            // input file (file ending determines if we are on the import/decompress side (.csgv) or can specify compression options (other)
            UnlabeledValueArg<std::string> inputpathArg("input", "Either a previously compressed .csgv file to render, or a segmentation volume file to compress or render. " CSGV_SYNTH_PREFIX_STR " to create and process a synthetic volume.", false, "", "(<volume file>|" CSGV_SYNTH_PREFIX_STR "[_args*])", cmd, true);

            // parse arguments
            cmd.parse(argc, argv);

            // general arguments
            va.verbose = verboseArg.getValue();
            va.headless = headlessArg.getValue();
#ifdef HEADLESS
            if (!va.headless) {
                throw ArgException("Volcanite was build with CMake option HEADLESS set. volcanite must be run with --headless option and can not use interactive windows.", headlessArg.longID());
            }
#endif
            va.decompress_export_file = expandPathStr(decompresspathArg.getValue());
            va.compress_export_file = expandPathStr(compresspathArg.getValue());
            {
                std::string op_codes = opMaskArg.getValue();
                std::transform(op_codes.begin(), op_codes.end(), op_codes.begin(), ::tolower);
                va.operation_mask = 0;
                for (int i = 0; i < op_codes.size(); i++) {
                    switch (op_codes.at(i)) {
                    case 'a':
                        va.operation_mask |= OP_ALL;
                        break;
                    case 'o':
                        va.operation_mask |= OP_ALL_WITHOUT_DELTA;
                        break;
                    case 'p':
                        va.operation_mask |= OP_PARENT_BIT;
                        break;
                    case 'x':
                        va.operation_mask |= OP_NEIGHBORX_BIT;
                        break;
                    case 'y':
                        va.operation_mask |= OP_NEIGHBORY_BIT;
                        break;
                    case 'z':
                        va.operation_mask |= OP_NEIGHBORZ_BIT;
                        break;
                    case 'n':
                        va.operation_mask |= OP_NEIGHBOR_BITS;
                        break;
                    case 'l':
                        va.operation_mask |= OP_PALETTE_LAST_BIT;
                        break;
                    case 'd':
                        // a "d-" instead of "d" switch enables using the old palette delta operations where only
                        // a single entry follows the delta operations and thus only deltas of 1<D<17 are supported
                        if (i + 1 < op_codes.size() && op_codes[i + 1] == '-') {
                            va.operation_mask |= OP_USE_OLD_PAL_D_BIT;
                            i++;
                            Logger(Info) << " JOOO ITS HERE LELELEL";
                        }
                        va.operation_mask |= OP_PALETTE_D_BIT;
                        break;
                    case 's':
                        va.operation_mask |= OP_STOP_BIT;
                        break;
                    default:
                        throw ArgException(opMaskArg.longID() + " must be a list of characters in p,x,y,z,n,l,d[-],s only", opMaskArg.longID());
                    }
                }
            }
            va.random_access = randomAccessArg.getValue();
            // rendering arguments
            {
                auto split_configs = renderconfigArg.getValue() | std::views::split(';') | std::views::transform([](auto r) -> std::string {
                                         // in C++20 this could be done in string views only
                                         // std::string_view v(r.data(), r.size());
                                         // v.remove_prefix(std::min(v.find_first_not_of(' '), v.size()));
                                         // v.remove_suffix(r.size() - 1u - std::min(v.find_last_not_of(' '), v.size()));
                                         std::string cfg;
                                         for (const char &c : r)
                                             cfg.push_back(c);
                                         // trim
                                         auto first = cfg.find_first_not_of(' ');
                                         auto last = cfg.find_last_not_of(' ');
                                         cfg = cfg.substr(first, last - first + 1);
                                         // expand file path (if it is a vcfg file)
                                         // and convert to strin
                                         if (cfg.ends_with(".vcfg"))
                                             return expandPathStr(cfg);
                                         return cfg;
                                     });
                va.rendering_configs = {split_configs.begin(), split_configs.end()};
            }
            va.screenshot_output_file = expandPathStr(imageArg.getValue());
            va.video_output_fmt_file = expandPathStr(videoArg.getValue());
            if (!va.video_output_fmt_file.empty()) {
                try {
                    size_t test_frame_idx = 1;
                    auto f = fmt::vformat(va.video_output_fmt_file, fmt::make_format_args(test_frame_idx));
                } catch (const fmt::format_error) {
                    throw ArgException(videoArg.longID() + " must be a formatted image file path string containing a single {} replacement field. Example: ./out{:04}.jpg", videoArg.longID());
                }
            }
            if (!resolutionArg.getValue().empty()) {
                std::stringstream ss(resolutionArg.getValue());
                ss >> va.render_resolution[0];
                ss.ignore();
                ss >> va.render_resolution[1];
                if (ss.fail())
                    throw ArgException(resolutionArg.longID() + " must have the format '[width]x[height]'", resolutionArg.longID());
                if (va.render_resolution[0] == 0u || va.render_resolution[1] == 0u)
                    throw ArgException(resolutionArg.longID() + " must contain positive integers only", resolutionArg.longID());
            }
            va.fullscreen = fullscreenArg.getValue();
            va.stream_lod = streamlodArg.getValue();
            va.cache_size_MB = cacheSizeMBArg.getValue();
            va.cache_palettized = cachePalettizedArg.getValue();
            if (va.cache_palettized && va.random_access)
                throw ArgException(cachePalettizedArg.longID() + " can not be used in combination with " + randomAccessArg.longID(), cachePalettizedArg.longID());
            switch (cacheModeArg.getValue()) {
            case 'n':
                va.cache_mode = CACHE_NOTHING;
                break;
            case 'v':
                va.cache_mode = CACHE_VOXELS;
                break;
            case 'b':
                va.cache_mode = CACHE_BRICKS;
                break;
            default:
                break;
            }
            va.decode_from_shared_memory = decodedSharedMemoryArg.getValue();
            if (va.decode_from_shared_memory && !va.random_access && !va.cache_mode == CACHE_BRICKS)
                throw ArgException(decodedSharedMemoryArg.longID() + " must be used in combination with " + randomAccessArg.longID() + " and " + cacheModeArg.longID() + " b",
                                   decodedSharedMemoryArg.longID());
            va.show_development_gui = devArg.getValue();
            va.enable_vsync = !noVsyncArg.getValue();
            va.empty_space_resolution = emptySpaceResolutionArg.getValue();
            if (va.cache_mode != CACHE_VOXELS && va.empty_space_resolution > 0u) {
                Logger(Warn) << "Empty space skipping grid (" << emptySpaceResolutionArg.longID()
                             << " only supported in combination with " << cacheModeArg.longID() << " v. Disabling.";
                va.empty_space_resolution = 0u;
            }
            // if no input file was specified, try to open a file dialog
            std::string input_file = inputpathArg.getValue();
            if (!input_file.starts_with(CSGV_SYNTH_PREFIX_STR))
                input_file = expandPathStr(input_file);
            else
                Logger(Debug) << getDummySegmentationVolumeHelpStr();
            input_volume_required = input_volume_required && !evalPrintArg.getValue();
            if (input_file.empty() && input_volume_required) {
#ifdef HEADLESS
                throw ArgException("Must provide input file in headless mode", inputpathArg.longID(""));
#else
                if (va.headless)
                    throw ArgException("Must provide input file in headless mode", inputpathArg.longID(""));
                if (!pfd::settings::available())
                    throw ArgException("Must provide input file as file dialogs are unavailable", inputpathArg.longID(""));

                // Open a file dialog to choose a file
                auto selected_file = pfd::open_file("Open Segmentation Volume", Paths::getHomeDirectory().string() + "/*",
                                                    {"Segmentation Volumes (.csgv .vti .hdf5 .h5 .raw .vraw .nrrd .nhdr)", "*.csgv *.vti *.hdf5 *.h5 *.raw *.vraw *.nrrd *.nhdr", "All Files", "*"});
                if (selected_file.result().empty()) {
                    throw ArgException("No input file was provided. Pass " CSGV_SYNTH_PREFIX_STR " as input file to create a synthetic volume.", inputpathArg.longID(""));
                }

                input_file = selected_file.result().at(0);
#endif
            }
            va.input_file = input_file;
            // some arguments depend on if we import a previously compressed .csgv file..
            if (input_file.ends_with(".csgv")) {
                // we could forbid to set any compression parameters at all if we are in this branch
                if (!va.compress_export_file.empty()) {
                    throw ArgException(compresspathArg.longID() + " can not be used with an already compressed .csgv input file", compresspathArg.longID());
                }

                va.working_dir = expandPath(input_file).parent_path();
            }
            // .. or if we compress a volume
            else {
                if (input_volume_required && !(input_file.starts_with(CSGV_SYNTH_PREFIX_STR) || input_file.ends_with(".vti") || input_file.ends_with(".raw") || input_file.ends_with(".vraw") || input_file.ends_with(".hdf5") || input_file.ends_with(".h5") || input_file.ends_with(".nrrd") || input_file.ends_with(".nhdr"))) {
                    throw ArgException("Unsupported input file ending (not in {.csgv|.vti|.hdf5|.h5|.raw|.vraw|.nrrd|.nhdr})", inputpathArg.longID(""));
                }

                // if (input_volume_required && !va.decompress_export_file.empty()) {
                //    throw ArgException(decompresspathArg.longID() + " can only be used with a .csgv input file.", decompresspathArg.longID());
                // }

                // set the working directory to store the csgv output volume, runtime configuration files etc.
                if (!va.compress_export_file.empty())
                    va.working_dir = expandPath(va.compress_export_file).parent_path();
                else {
                    va.working_dir = std::filesystem::temp_directory_path() / "volcanite";
                }

                // attribute arguments (if we import a .csgv file, the attributes are already stored in a database along with it)
                va.label_remapping = labelRemappingArg.getValue();
                if (!attributeArg.getValue().empty()) {
                    va.label_remapping = true;

                    // the attribute argument string is a list of arguments itself:

                    const std::string &attribute_info = attributeArg.getValue();
                    auto comma0 = attribute_info.find(',', 0);
                    auto comma1 = attribute_info.find(',', comma0 + 1);

                    va.attribute_database = attribute_info.substr(0, comma0);
                    if (!std::filesystem::exists(va.attribute_database))
                        throw ArgException(attributeArg.longID() +
                                               " attribute database file does not exists or can not be accessed.",
                                           attributeArg.longID());
                    // csv file (only contains one table so no table name is specified)
                    // -a filename.csv[,label_column_name[,csv_separator]]
                    if (va.attribute_database.ends_with("csv")) {
                        va.attribute_table = "";
                        if (comma0 != std::string::npos)
                            va.attribute_label = attribute_info.substr(comma0 + 1, (comma1 - comma0 - 1));
                        else
                            va.attribute_label = "";

                        if (comma1 != std::string::npos)
                            va.attribute_csv_separator = attribute_info.substr(comma1 + 1);
                        else
                            va.attribute_csv_separator = ",";
                        // the separator may be encapsulated by "" or ''
                        if (va.attribute_csv_separator.length() > 2 &&
                                (va.attribute_database.front() == '"' && va.attribute_database.back() == '"') ||
                            (va.attribute_database.front() == '\'' && va.attribute_database.back() == '\'')) {
                            va.attribute_csv_separator = va.attribute_csv_separator.substr(1, va.attribute_csv_separator.length() - 2);
                        } else if (va.attribute_csv_separator.empty()) {
                            va.attribute_csv_separator = ',';
                        }
                    }
                    // sqlite or db3 (SQLite Data Base)
                    // -a filename.{db3|sqlite}[,table_name[,label_column_name]]
                    else {
                        if (comma0 != std::string::npos)
                            va.attribute_table = attribute_info.substr(comma0 + 1, (comma1 - comma0 - 1));
                        else
                            va.attribute_table = "";

                        if (comma1 != std::string::npos)
                            va.attribute_label = attribute_info.substr(comma1 + 1);
                        else
                            va.attribute_label = "";
                    }

                    if (!std::filesystem::exists(va.attribute_database))
                        throw ArgException(attributeArg.longID() + " attribute database file does not exists or can not be accessed.", attributeArg.longID());
                }

                // compression arguments
                va.brick_size = bricksizeArg.getValue();
                if (va.random_access) {
                    constexpr EncodingMode _strengths[] = {NIBBLE_ENC, WAVELET_MATRIX_ENC, HUFFMAN_WM_ENC};
                    va.encoding_mode = _strengths[strengthArg.getValue()];
                    // Nibble encoding does not support PALETTE_DELTA and STOP_BITS
                    if (va.operation_mask & OP_PALETTE_D_BIT) {
                        va.operation_mask = va.operation_mask & ~OP_PALETTE_D_BIT;
                        Logger(Warn) << "Encoding with random access does not support palette delta operation. Disabling.";
                    }
                    if (va.encoding_mode == NIBBLE_ENC && va.operation_mask & OP_STOP_BIT) {
                        va.operation_mask = va.operation_mask & ~OP_STOP_BIT;
                        Logger(Warn) << "Nibble encoding with random access does not support stop bits. Disabling.";
                    }
                } else {
                    constexpr EncodingMode _strengths[] = {NIBBLE_ENC, SINGLE_TABLE_RANS_ENC, DOUBLE_TABLE_RANS_ENC};
                    va.encoding_mode = _strengths[strengthArg.getValue()];
                }
                va.freq_subsampling = subsamplingArg.getValue();
                va.threads = threadsArg.getValue();
                va.chunked = !chunkedArg.getValue().empty();
                if (va.chunked) {
                    if (va.compress_export_file.empty())
                        throw ArgException("A csgv export path must be specified with " + compresspathArg.longID() + " when processing chunked volumes!");

                    const std::string &chunk_indices = chunkedArg.getValue();
                    std::stringstream ss(chunk_indices);
                    ss >> va.chunk_files[0];
                    ss.ignore();
                    ss >> va.chunk_files[1];
                    ss.ignore();
                    ss >> va.chunk_files[2];
                    if (ss.fail())
                        throw ArgException(chunkedArg.longID() + " must have the format 'xn,yn,zn' with *n being integer numbers", chunkedArg.longID());
                    if (va.chunk_files[0] == 0u && va.chunk_files[1] == 0u && va.chunk_files[2] == 0u)
                        throw ArgException(chunkedArg.longID() + " inclusive xn,yn,zn range must contain at least 2 chunks", chunkedArg.longID());

                    // count occurrences of {} in the string. It must be exactly 3 and there must be at least one
                    // character between consecutive placeholders {}.
                    {
                        try {
                            uint32_t test_chunk_idx = 1;
                            auto f = fmt::vformat(va.input_file, fmt::make_format_args(test_chunk_idx, test_chunk_idx, test_chunk_idx));
                        } catch (const fmt::format_error) {
                            throw ArgException("input volume must be a formatted file path string containing three {} keys to be replaced with x,y,z chunk indices. Example: ./x{}y{}z{}.hdf5", inputpathArg.longID(""));
                        }
                    }
                }
                va.run_tests = testArg.getValue();
            }
            va.export_stats = statsArg.getValue();
            va.record_in_file = expandPathStr(recordInFileArg.getValue());
            va.record_convergence_frames = recordConvergenceArg.getValue();
            std::string comma_separated_logfiles = evalLogFilesArg.getValue();
            va.eval_logfiles.clear();
            for (const auto& logfile : comma_separated_logfiles | std::views::split(',') | std::views::transform([](const auto &&range) -> std::string {
                                                       // string_view and string constructors do not accept the range iterators in C++17
                                                       std::string tmp;
                                                       for (const char c : range)
                                                           tmp.push_back(c);
                                                       return tmp;
                                                   })) {
                va.eval_logfiles.emplace_back(expandPathStr(std::string(logfile)));
                // TODO: check if the logfiles contain valid format strings
            }
            va.eval_name = evalNameArg.getValue();
            if (!va.eval_name.empty() && va.eval_logfiles.empty()) {
                throw ArgException("Evaluation name must be used in combination with --eval-logfile",
                                   evalNameArg.longID(""));
            }
            va.print_eval_keys = evalPrintArg.getValue();
            va.shader_defines = shaderDefineArg.getValue();
            std::ranges::replace(va.shader_defines, ';', ' ');

            return va;
        } catch (TCLAP::ArgException &e) {
#ifdef _WIN32
            vvv::Logger(Error) << "argument error: " << e.error() << " for " << e.argId();
#else
            vvv::Logger(vvv::Error) << "argument error: " << e.error() << " for " << e.argId();
#endif
        }

        return {};
    }
};

} // namespace volcanite
