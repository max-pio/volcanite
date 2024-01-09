#pragma once

#include <string>
#include <sstream>
#include <iostream>
#include <optional>
#include <tclap/CmdLine.h>
#include "portable-file-dialogs.h"

#include "vvv/util/Logger.hpp"
#include "volcanite/compression/CompressedSegmentationVolume.hpp"

namespace vvv {

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
    bool show_development_gui = false;

    // ToDo: attribute args
    // std::string attribute_file;     // sqlite3 file with attributes for volume labels
    // std::string label_attribute;    // name of the label attribute. if empty: first column in attribute_file
    // bool label_remapping = false;   // if label ids in the volume should be remapped to a consecutive interval

    // compression args
    std::string compress_export_file;      // !empty = perform compression to file         Only one of
    std::string decompress_export_file;    // !empty = perform decompression to file       both can be set!
    std::string segmented_volume_file;
    uint32_t brick_size = 32;
    CompressedSegmentationVolume::RANSMode rANS_mode = CompressedSegmentationVolume::RANSMode::DOUBLE_TABLE_RANS;
    uint32_t freq_subsampling = 8;     // n^3 factor for subsampling bricks for frequency table computation with rANS

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

        // ToDo: update to TCLAP Version 1.4 to be able to group rendering and compression parameters with AnyOf, EitherOf?
        using namespace TCLAP;
        try {
            CmdLine cmd(getHelpString(), ' ', getVolcaniteVersionString());

            // compression arguments
            ValueArg<std::string> decompresspathArg("d", "decompress", "Export the decompressed volume to given file.", false, va.decompress_export_file, "file", cmd);
            ValueArg<std::string> compresspathArg("c", "compress", "Export the compressed volume to given csgv file.", false, va.compress_export_file, "file", cmd);
            ValueArg<std::string> chunkedArg("", "chunked", "Compress chunked segmented volume using formatted <volume> path with inclusive x, y, and z chunk file ranges as: \".*{[0..<xn>]}.*{[0..<yn>]}.*{[0..<zn>]}.*\".", false, "", "xn,yn,zn", cmd);
            ValueArg<uint32_t> subsamplingArg("", "freq-sampling", "Compression prepass acceleration by given factor cubed. Affects strength 1 or 2 only.", false, va.freq_subsampling, "int", cmd);
            ValueArg<uint32_t> threadsArg("", "threads", "Number of CPU threads for (de)compression parallelization.", false, va.threads, "int", cmd);
            ValuesConstraint<uint32_t> allowedStrength({0u, 1u, 2u});
            ValueArg<uint32_t> strengthArg("s", "strength", "Compress with more expensive but stronger variable bit-length encoding (1). Use two frequency tables for even stronger compression (2).", false, 2, &allowedStrength);
            cmd.add(strengthArg);
            ValuesConstraint<uint32_t> allowedBrickSize({8u, 16u, 32u, 64u, 128u});
            ValueArg<uint32_t> bricksizeArg("b", "brick-size", "Compress with given brick size.", false, va.brick_size, &allowedBrickSize);
            cmd.add(bricksizeArg);
            // rendering arguments
            SwitchArg devArg("", "dev", "Reveal all development render parameters in GUI.", cmd);
            ValueArg<uint32_t> cachesizeArg("", "cache-size", "Size in MB to allocate for GPU renderer brick cache.", false, va.cache_size_MB, "size", cmd);
            SwitchArg streamlodArg("", "stream-lod", "Stream finest level of detail to GPU on demand. Helps with low GPU memory.", cmd);
            ValueArg<std::string> imageArg("i", "image", "Renders an image to the given file on startup", false, va.screenshot_output_file, "file", cmd);
            ValueArg<std::string> resolutionArg("r", "resolution", "Startup render resolution as [Width]x[Height]", false, "", "file", cmd);
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
            va.decompress_export_file = decompresspathArg.getValue();
            va.compress_export_file = compresspathArg.getValue();
            // rendering arguments
            va.rendering_config_file = renderconfigArg.getValue();
            va.screenshot_output_file = imageArg.getValue();
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
            va.cache_size_MB = cachesizeArg.getValue();
            va.show_development_gui = devArg.getValue();
            // if no input file was specified, try to open a file dialog
            std::string input_file = inputpathArg.getValue();
            if(input_file.empty()) {
                if(va.headless)
                    throw ArgException("Must provide input file in headless mode", inputpathArg.longID());
                if (!pfd::settings::available())
                    throw ArgException("Must provide input file as file dialogs are unavailable", inputpathArg.longID());

                // Open a file dialog to choose a file
                auto selected_file = pfd::open_file("Open Segmentation Volume", pfd::path::home(),
                                                    { "Segmentation Volumes (.csgv .vti .hdf5 .raw)", "*.csgv *.vti *.hdf5 *.raw", "All Files", "*" });
                if(selected_file.result().empty()) {
                    throw ArgException("No input file was provided", inputpathArg.longID());
                }

                input_file = selected_file.result().at(0);
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
                if(!(input_file.ends_with(".vti") || input_file.ends_with(".raw") || input_file.ends_with(".hdf5"))) {
                    throw ArgException("Unsupported input file ending (not in {.csgv|.vti|.hdf5|.raw})", inputpathArg.longID());
                }

                if(!va.decompress_export_file.empty()) {
                    throw ArgException(decompresspathArg.longID() + " can only be used with a .csgv input file", decompresspathArg.longID());
                }

                // compression arguments
                va.brick_size = bricksizeArg.getValue();
                const CompressedSegmentationVolume::RANSMode _strengths[] = {CompressedSegmentationVolume::NO_RANS, CompressedSegmentationVolume::SINGLE_TABLE_RANS, CompressedSegmentationVolume::DOUBLE_TABLE_RANS};
                va.rANS_mode = _strengths[strengthArg.getValue()];
                va.freq_subsampling = subsamplingArg.getValue();
                va.threads = threadsArg.getValue();
                va.chunked = !chunkedArg.getValue().empty();
                if(va.chunked) {
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
                        size_t pos = va.input_file.find("{}", pos);
                        while (pos < va.input_file.length() && count < 4) {
                            size_t last_pos = pos;
                            pos = va.input_file.find("{}", pos + 1);
                            if (pos - last_pos < 3)
                                throw ArgException(
                                        "Input file path must contain at least one other character between consecutive {} for x,y,z indices in chunked data",
                                        inputpathArg.longID());
                            count++;
                        }
                        if (count != 3)
                            throw ArgException(
                                    "Input file path must contain exactly three placeholders {} for x,y,z indices in chunked data",
                                    inputpathArg.longID());
                    }
                }
            }

            return va;
        }
        catch (TCLAP::ArgException &e) {
            vvv::Logger(vvv::ERROR) << "argument error: " << e.error() << " for " << e.argId();
        }

        return {};
    }
};

} // namespace vvv
