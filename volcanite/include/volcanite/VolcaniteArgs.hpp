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

#include <string>
#include <sstream>
#include <iostream>
#include <optional>
#include <tclap/CmdLine.h>
#ifndef HEADLESS
    #include "portable-file-dialogs.h"
#endif

#include "vvv/util/Logger.hpp"
// TODO: Split the rANS-Mode etc into a separate Header / forward decl
#include "volcanite/compression/CompressedSegmentationVolume.hpp"

using namespace vvv;

namespace volcanite {

struct VolcaniteArgs {

public:
    enum Mode {
        NO_RENDERING = 0,
        HEADLESS_RENDERING = 1,
        GUI_APP_RENDERING = 2,
    };

    // general args
    bool verbose = false;
    bool headless = false;
    std::string input_file;                  // must be .csgv if compress is false, otherwise vti / raw / hdf5
    bool chunked = false;                    // if the first 3 {} in the input string should be chunk ids formatted
    uint32_t chunk_files[3] = {0u, 0u, 0u};  // max. xyz index of chunk files. e.g. (1,3,0) would load 8 chunk files
    uint32_t threads = 0;                    // number of CPU threads (0 = system supported concurrent threads)

    // rendering args
    Mode rendering_mode = GUI_APP_RENDERING;
    std::string rendering_config_file;
    std::string screenshot_output_file;
    uint32_t render_resolution[2] = {1920, 1080};
    bool stream_lod;
    size_t cache_size_MB = 1024ul;
    bool cache_palettized = false;
    bool show_development_gui = false;

    // attribute args
    std::string attribute_database;     // SQlite3 file with attributes for volume labels
    std::string attribute_table;        // table or view containing the attributes for the volume labels
    std::string attribute_label;        // name of the label attribute
    bool label_remapping = false;       // if label ids in the volume should be remapped to a consecutive interval

    // compression args
    std::string compress_export_file;   // !empty = perform compression to file         Only one of
    std::string decompress_export_file; // !empty = perform decompression to file       both can be set!
    std::string segmented_volume_file;
    uint32_t brick_size = 32;
    RANSMode rANS_mode = RANSMode::DOUBLE_TABLE_RANS;
    uint32_t freq_subsampling = 8;      // n^3 factor for subsampling bricks for frequency table computation with rANS
    bool random_access = false;         // encode bricks so that they support random access within a brick

    bool run_tests = false;
    bool export_stats = false;


    static std::string getHelpString() {
        std::stringstream ss;
        ss << "EXAMPLES:" << std::endl;
        ss << "./volcanite --headless -r 1920x1080 -i screenshot.png volume.vti" << std::endl <<
        "\tExports a render image without starting the application." << std::endl;
        ss << "./volcanite --headless -b 64 -s 2 -c out.csgv volume.vti" << std::endl <<
        "\tExports a strongly compressed volume." << std::endl;
        ss << "./volcanite --headless -d out.vti volume.csgv" << std::endl <<
        "\tDecompresses volume.csgv to out.vti." << std::endl;
        ss << "./volcanite volume" << std::endl <<
        "\tStarts the Volcanite renderer for the given volume." << std::endl;
        ss << "./volcanite --config low.cfg --cache-size 512 -b 32 -s 2 --freq-sampling 8 --stream-lod volume.vti" << std::endl <<
        "\tStarts Volcanite for limited GPU capabilities." << std::endl;
        ss << "./volcanite --headless -c out.csgv --chunked 1,3,0 vol_x{}_y{]_z{}.vti" << std::endl <<
       "\tCompresses chunked volume vol_x0_y0_z0.vti to vol_x1_y3_z0.vti." << std::endl;
        return ss.str();
    };

    static std::string getVolcaniteVersionString() {
        return VOLCANITE_VERSION;
    }

    bool performCompression() {
        return !input_file.ends_with(".csgv");
    }

    bool performDecompression() {
        return !decompress_export_file.empty();
    }

    static std::optional<VolcaniteArgs> parseArguments(int argc, char *argv[]) {
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
            SwitchArg testArg("t", "test", "Run test after performing the compression", cmd);
            SwitchArg statsArg("", "stats", "Export statistics after performing the compression", cmd);
            SwitchArg randomAccessArg("r", "random-access", "Encode in a format that supports random access and in-brick parallelism for the decompression.", cmd);

            // attribute arguments
            SwitchArg labelRemappingArg("", "relabel", "Relabel the voxel labels even if no attribute database is used.", cmd);
            ValueArg<std::string> attributeArg("a", "attribute", "SQLite attribute database as: \"{database filepath}[,{attribute table/view name}[,{label column name referenced by volume}]]\".", false, "", "database[,table[,label]]", cmd);
            // rendering arguments
            SwitchArg devArg("", "dev", "Reveal all development render parameters in GUI.", cmd);
            ValueArg<uint32_t> cacheSizeMBArg("", "cache-size", "Size in MB of the renderer's brick cache. 0 to allocate all available.", false, va.cache_size_MB, "size", cmd);
            SwitchArg cachePalettizedArg("", "cache-palette", "Store palette indices in brick cache instead of labels.", cmd);
            SwitchArg streamlodArg("", "stream-lod", "Stream finest level of detail to GPU on demand. Helps with low GPU memory.", cmd);
            ValueArg<std::string> imageArg("i", "image", "Renders an image to the given file on startup.", false, va.screenshot_output_file, "file", cmd);
            ValueArg<std::string> resolutionArg("r", "resolution", "Startup render resolution as [Width]x[Height].", false, "", "file", cmd);
            ValueArg<std::string> renderconfigArg("", "config", "Import render parameters from config file.", false, va.rendering_config_file, "file", cmd);
            // general arguments
            SwitchArg headlessArg("", "headless", "Do not start GUI application.", cmd);
            SwitchArg verboseArg("v", "verbose", "Verbose debug output.", cmd);

            // input file (file ending determines if we are on the import/decompress side (.csgv) or can specify compression options (other)
            UnlabeledValueArg<std::string> inputpathArg("input", "Either a previously compressed .csgv file to render or a segmented volume to compress or render.", false, "", "volume", cmd, true);

            // parse arguments
            cmd.parse(argc, argv);

            // general arguments
            va.verbose = verboseArg.getValue();
            va.headless = headlessArg.getValue();
#ifdef HEADLESS
            if(!va.headless) {
                throw ArgException("Volcanite was build with CMake option HEADLESS set. You must run volcanite with --headless option and can not view interactive windows.", headlessArg.longID());
            }
#endif
            va.decompress_export_file = expandPath(decompresspathArg.getValue());
            va.compress_export_file = expandPath(compresspathArg.getValue());
            va.export_stats = statsArg.getValue();
            va.random_access = randomAccessArg.getValue();
            // rendering arguments
            va.rendering_config_file = expandPath(renderconfigArg.getValue());
            va.screenshot_output_file = expandPath(imageArg.getValue());
            if(!resolutionArg.getValue().empty()) {
                std::stringstream ss(resolutionArg.getValue());
                ss >> va.render_resolution[0];
                ss.ignore();
                ss >> va.render_resolution[1];
                if (ss.fail())
                    throw ArgException(resolutionArg.longID() + " must have the format '[width]x[height]'", resolutionArg.longID());
                if(va.render_resolution[0] == 0u || va.render_resolution[1] == 0u)
                    throw ArgException(resolutionArg.longID() + " must contain positive integers only", resolutionArg.longID());
            }
            va.stream_lod = streamlodArg.getValue();
            va.cache_size_MB = cacheSizeMBArg.getValue();
            va.cache_palettized = cachePalettizedArg.getValue();
            if(va.cache_palettized && va.random_access)
                throw ArgException(cachePalettizedArg.longID() + " can not be used in combination with " + randomAccessArg.longID(), cachePalettizedArg.longID());
            va.show_development_gui = devArg.getValue();
            // if no input file was specified, try to open a file dialog
            std::string input_file = expandPath(inputpathArg.getValue());
            if(input_file.empty()) {
#ifdef HEADLESS
                throw ArgException("Must provide input file in headless mode", inputpathArg.longID(""));
#else
                if(va.headless)
                    throw ArgException("Must provide input file in headless mode", inputpathArg.longID(""));
                if (!pfd::settings::available())
                    throw ArgException("Must provide input file as file dialogs are unavailable", inputpathArg.longID(""));

                // Open a file dialog to choose a file
                auto selected_file = pfd::open_file("Open Segmentation Volume", pfd::path::home(),
                                                    { "Segmentation Volumes (.csgv .vti .hdf5 .h5 .raw .vraw .nrrd .nhdr)", "*.csgv *.vti *.hdf5 *.h5 *.raw *.vraw *.nrrd *.nhdr", "All Files", "*" });
                if(selected_file.result().empty()) {
                    throw ArgException("No input file was provided", inputpathArg.longID(""));
                }

                input_file = selected_file.result().at(0);
#endif
            }
            va.input_file = input_file;
            // some arguments depend on if we import a previously compressed .csgv file..
            if(input_file.ends_with(".csgv")) {
                // we could forbid to set any compression parameters at all if we are in this branch

                if(!va.compress_export_file.empty()) {
                    throw ArgException(compresspathArg.longID() + " can not be used with an already compressed .csgv input file", compresspathArg.longID());
                }
            }
            // .. or if we compress a volume
            else {
                if(!(input_file.ends_with(".vti")
                    || input_file.ends_with(".raw") || input_file.ends_with(".vraw")
                    || input_file.ends_with(".hdf5") || input_file.ends_with(".h5")
                    || input_file.ends_with(".nrrd") || input_file.ends_with(".nhdr"))) {
                    throw ArgException("Unsupported input file ending (not in {.csgv|.vti|.hdf5|.h5|.raw|.vraw|.nrrd|.nhdr})", inputpathArg.longID(""));
                }

                if(!va.decompress_export_file.empty()) {
                    throw ArgException(decompresspathArg.longID() + " can only be used with a .csgv input file.", decompresspathArg.longID());
                }

                // attribute arguments (if we import a .csgv file, the attributes are already stored in a database along with it)
#ifndef LIB_SQLITE3
                if(labelRemappingArg.getValue()) {
                    throw ArgException(labelRemappingArg.longID() + " must not be set as SQLite3 library is not available.", labelRemappingArg.longID());
                }
                if(!attributeArg.getValue().empty()) {
                    throw ArgException(attributeArg.longID() + " is not available as SQLite3 library is not available.", attributeArg.longID());
                }
                va.label_remapping = false;
                va.attribute_database = "";
                va.attribute_table = "";
                va.attribute_label = "";
#else
                va.label_remapping = labelRemappingArg.getValue();
                if(!attributeArg.getValue().empty()) {
                    va.label_remapping = true;
                    const std::string attribute_info = attributeArg.getValue();
                    auto comma0 = attribute_info.find(',', 0);
                    auto comma1 = attribute_info.find(',', comma0 + 1);

                    va.attribute_database = attribute_info.substr(0, comma0);
                    if(comma0 != std::string::npos)
                        va.attribute_table = attribute_info.substr(comma0+1, (comma1 - comma0-1));
                    else
                        va.attribute_table = "";

                    if(comma1 != std::string::npos)
                        va.attribute_label = attribute_info.substr(comma1+1);
                    else
                        va.attribute_label = "";

                    if(!std::filesystem::exists(va.attribute_database))
                        throw ArgException(attributeArg.longID() + " attribute database file does not exists or can not be accessed.", attributeArg.longID());
                }
#endif

                // compression arguments
                va.brick_size = bricksizeArg.getValue();
                const RANSMode _strengths[] = {NO_RANS, SINGLE_TABLE_RANS, DOUBLE_TABLE_RANS};
                va.rANS_mode = _strengths[strengthArg.getValue()];
                va.freq_subsampling = subsamplingArg.getValue();
                va.threads = threadsArg.getValue();
                va.chunked = !chunkedArg.getValue().empty();
                if(va.chunked) {
                    if(va.compress_export_file.empty())
                        throw ArgException("A csgv export path must be specified with " + compresspathArg.longID() + " when processing chunked volumes!");

                    std::string chunk_indices = chunkedArg.getValue();
                    std::stringstream ss(chunk_indices);
                    ss >> va.chunk_files[0];
                    ss.ignore();
                    ss >> va.chunk_files[1];
                    ss.ignore();
                    ss >> va.chunk_files[2];
                    if (ss.fail())
                        throw ArgException(chunkedArg.longID() + " must have the format 'xn,yn,zn' with *n being integer numbers", chunkedArg.longID());
                    if(va.chunk_files[0] == 0u && va.chunk_files[1] == 0u && va.chunk_files[2] == 0u)
                        throw ArgException(chunkedArg.longID() + " inclusive xn,yn,zn range must contain at least 2 chunks", chunkedArg.longID());

                    // count occurrences of {} in the string. It must be exactly 3 and there must be at least one
                    // character between consecutive placeholders {}.
                    {
                        int count = 0;
                        size_t pos = va.input_file.find("{}");
                        while (pos < va.input_file.length() && count < 4) {
                            count++;
                            size_t last_pos = pos;
                            pos = va.input_file.find("{}", pos + 1);
                            if (pos - last_pos < 3)
                                throw ArgException(
                                        "Input file path must contain at least one other character between consecutive {} for x,y,z indices in chunked data",
                                        inputpathArg.longID(""));
                        }
                        if (count != 3)
                            throw ArgException(
                                    "Input file path must contain exactly three placeholders {} for x,y,z indices in chunked data",
                                    inputpathArg.longID(""));
                    }
                }
                va.run_tests = testArg.getValue();
            }

            return va;
        }
        catch (TCLAP::ArgException &e) {
#ifdef _WIN64
            vvv::Logger(ERROR) << "argument error: " << e.error() << " for " << e.argId();
#else
            vvv::Logger(vvv::ERROR) << "argument error: " << e.error() << " for " << e.argId();
#endif
        }

        return {};
    }
};

} // namespace volcanite
