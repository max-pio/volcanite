#pragma once

#include <string>
#include <iostream>

#include <tclap/CmdLine.h>
#include <optional>
#include "vvv/util/Logger.hpp"

namespace vvv {

    struct VolcaniteArgs {

    public:
        enum Mode {
            NO_RENDERING = 0,
            HEADLESS_RENDERING = 1,
            GUI_APP_RENDERING = 2,
        };

        // general args
        bool printVersion = false;
        bool printHelp = false;
        bool verbose = false;
        std::string csgv_file;          // input if compress is false, otherwise compression output and render input file

        // rendering args
        Mode rendering_mode = GUI_APP_RENDERING;
        std::string rendering_config_file;
        std::string screenshot_output_file;
        bool stream_lod;
        size_t cache_size_MB = 1024ul;

        // ToDo: attribute args
        // std::string attribute_file;     // sqlite3 file with attributes for volume labels
        // std::string label_attribute;    // name of the label attribute. if empty: first column in attribute_file

        // compression args
        bool compress = false;
        std::string segmented_volume_file;
        bool chunked = false;
        int compression_strength = 2;   // low (0), rANS VBE (1), rANS VBE with double frequency table (2)
        uint32_t freq_subsampling = 8;  // n^3 factor for subsampling bricks for frequency table computation with rANS
        bool label_remapping = true;    // if label ids in the volume should be remapped to a consecutive interval



        void printHelpString(std::ostream out) const {
            out << R"H3LP(
Usage:
volcanite [<general_args>] [<rendering_args>] [<csgv_file> [--decompress <file>] |
                                               [<compression_args>] <segmented_volume_file>]

<general_args>
--help -h                   Displays the help message
--version                   Displays the Volcanite version
--verbose -v                Print all debug output
--headless -h               Does not start the GUI application


<rendering_args>
--config <file>          Imports rendering parameters from config file
--image -i <file>           Renders an image to <file> on startup
--stream-lod                Stream finest level of detail to GPU on demand
                            which helps with insufficient GPU memory
--cache-size <size_in_mb>   Size in MB to allocate for GPU renderer brick cache

<compression_args>
--bricksize -b <size>       Use power of two brick size <size>
--strength -s [0|1|2]       Add more expensive variable bit-length encoding (1)
                            with two frequency tables (2)
--freq-sampling -f <factor> Accelerate strength 1 or 2 prepass by <factor>³ > 0
--chunked                   Accept formatted <segmented_volume_file> for
                            chunked data with inclusive x, y, and z ranges as
                            ".*{<x0>-<xn>}.*{<y0>-<yn>}.*{<z0>-<zn>}.*"
--compress -c <file>        Export the compressed volume to csgv file <file>
)H3LP";
        }

        static std::string getVolcaniteVersion() {
            return VOLCANITE_VERSION;
        }

        void printVersionString(std::ostream out) const {
            out << getVolcaniteVersion();
        }

        static std::optional<VolcaniteArgs> parseArguments(int argc, char *argv[]) {
            VolcaniteArgs va;

            using namespace TCLAP;
            try {
                const char* help_text =  R"H3LP(
volcanite [<general_args>] [<rendering_args>] [ [--decompress <file>] <csgv_file> |
                                                [<compression_args>] <segmented_volume_file>]

<general_args>
--help -h                   Displays the help message
--version                   Displays the Volcanite version
--verbose -v                Print all debug output
--headless -h               Does not start the GUI application

<rendering_args>
--config <file>             Imports rendering parameters from config file
--image -i <file>           Renders an image to <file> on startup
--stream-lod                Stream finest level of detail to GPU on demand
                            which helps with insufficient GPU memory
--cache-size <size_in_mb>   Size in MB to allocate for GPU renderer brick cache

<compression_args>
--bricksize -b <size>       Use power of two brick size <size>
--strength -s [0|1|2]       Add more expensive variable bit-length encoding (1)
                            with two frequency tables (2)
--freq-sampling -f <factor> Accelerate strength 1 or 2 prepass by <factor>³ > 0
--chunked                   Accept formatted <segmented_volume_file> for
                            chunked data with inclusive x, y, and z ranges as
                            ".*{<x0>-<xn>}.*{<y0>-<yn>}.*{<z0>-<zn>}.*"
--compress -c <file>        Export the compressed volume to csgv file <file>
                )H3LP";
                CmdLine cmd(help_text, ' ', getVolcaniteVersion());

                // parse arguments
                SwitchArg verboseArg("v", "verbose", "Verbose debug output", cmd);
                SwitchArg headlessArg("h", "headless", "Do not start GUI application.", cmd);
                // ToDo..
                ValueArg<std::string> renderconfigArg("", "config", "Render parameter config file.", false, va.rendering_config_file, "path", cmd);


                cmd.parse(argc, argv);

                // overwrite default values in VolcaniteArgs struct when arguments were given on the command line
                va.rendering_config_file = renderconfigArg.getValue();
                // ToDo..

                return va;
            }
            catch (TCLAP::ArgException &e) {
                vvv::Logger(vvv::ERROR) << "argument parsing error: " << e.error() << " for arg " << e.argId();
            }

            return {};
        }
    };

} // namespace vvv
