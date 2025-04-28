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

#include "volcanite/util/args_and_csgv_provider.hpp"
#include "volcanite/CSGVPathUtils.hpp"
#include "volcanite/compression/CompSegVolHandler.hpp"
#include "volcanite/eval/EvaluationLogExport.hpp"

#include "vvv/util/Logger.hpp"
#include "vvv/util/detect_debugger.hpp"

#include <string>

namespace volcanite {

int volcanite_provide_args_and_csgv(VolcaniteArgs &args,
                                    std::shared_ptr<CompressedSegmentationVolume> &compressedSegmentationVolume,
                                    std::shared_ptr<CSGVDatabase> &csgvDatabase,
                                    int argc, char *argv[]) {

    compressedSegmentationVolume = nullptr;
    csgvDatabase = nullptr;

    // parse command line arguments
    {
        auto _args = VolcaniteArgs::parseArguments(argc, argv);
        if (!_args.has_value()) {
            Logger(Error) << "Exiting because of invalid arguments. See volcanite --help for available commands.";
            return RET_INVALID_ARG;
        }
        args = _args.value();
        if (args.print_eval_keys) {
            Logger(Info) << "Available evaluation log keys (used with --eval-log):";
            const auto keys = EvaluationLogExport::get_all_evaluation_keys();
            for (const auto &key : keys) {
                Logger(Info) << " " << key;
            }
            return RET_SUCCESS;
        }
    }

    if (!vvv::debuggerIsAttached() && !args.verbose)
        Logger::s_minLevel = Info;

    // if we have to compress the input file (.vti/.raw/.hdf5..) we do it here
    if (args.performCompression()) {
        glm::uvec3 max_chunk_id = glm::uvec3(args.chunk_files[0], args.chunk_files[1], args.chunk_files[2]);
        if (!args.verbose) {
            Logger(Info) << "compressing segmentation volume " << args.input_file
                         << (args.chunked ? " with max. chunks " + str(max_chunk_id) : "");
        }

        std::string complete_csgv_path = {};
        bool use_temporary_output_file = args.compress_export_file.empty();
        // if no output file is specified, we try to export the .csgv file to the location of the input file
        if (!use_temporary_output_file) {
            complete_csgv_path = args.compress_export_file;
        } else if (!args.input_file.starts_with(CSGV_SYNTH_PREFIX_STR)) {
            std::string potential_path = stripFileExtension(args.input_file) + ".csgv";
            // this only works if the input path is not a formatted chunked input path,
            if (!args.chunked && !std::filesystem::exists(potential_path)) {
                std::ofstream file;
                file.open(potential_path);
                if (file.is_open()) {
                    file.close();
                    std::filesystem::remove(potential_path);
                    complete_csgv_path = potential_path;
                    use_temporary_output_file = false;
                }
            }
        }

        // otherwise, we just use a tmp file
        if (use_temporary_output_file) {
            if (args.working_dir.empty())
                args.working_dir = std::filesystem::temp_directory_path() / "volcanite";
            create_directory(args.working_dir);
            complete_csgv_path = (args.working_dir / "tmp.csgv").string();
            if (std::filesystem::exists(complete_csgv_path))
                std::filesystem::remove(complete_csgv_path);
        }

        if (!args.label_remapping && !args.attribute_database.empty()) {
            Logger(Error) << "Attribute database can not be used without label remapping. Aborting.";
            return RET_INVALID_ARG;
        }

        // we open a precomputed csgv database for this volume if it exists or create it otherwise
        std::shared_ptr<std::unordered_map<uint32_t, uint32_t>> label_remapping = nullptr;
        csgvDatabase = std::make_shared<CSGVDatabase>();
        if (args.label_remapping) {
            std::string database_path = stripFileExtension(complete_csgv_path) + "_csgv.db3";
            if (use_temporary_output_file && std::filesystem::exists(database_path))
                std::filesystem::remove(database_path);

            MiniTimer t;
            Logger(Info) << "Initializing attribute database " << database_path;
            csgvDatabase->importOrProcessChunkedVolume(args.input_file, database_path,
                                                       args.attribute_database, args.attribute_table,
                                                       args.attribute_label, args.attribute_csv_separator,
                                                       args.chunked, max_chunk_id);
            // obtain the label re-mapping from the database
            label_remapping = csgvDatabase->getLabelRemapping();
            if (args.verbose)
                Logger(Info) << "  finished in " << t.elapsed() << " seconds";
        } else {
            csgvDatabase->createDummy();
        }

        CompSegVolHandler::CSGVCompressionConfig cfg = {.brick_dim = static_cast<int>(args.brick_size),
                                                        .encoding_mode = args.encoding_mode,
                                                        .op_mask = args.operation_mask,
                                                        .random_access = args.random_access,
                                                        .label_remapping = label_remapping,
                                                        .cpu_threads = args.threads,
                                                        .use_detail_separation = args.stream_lod,
                                                        .force_recompute = !args.chunked,
                                                        .chunked_input_data = args.chunked,
                                                        .max_file_index = max_chunk_id,
                                                        .freq_subsampling = args.freq_subsampling,
                                                        .run_tests = args.run_tests,
                                                        .export_stats_per_chunk = args.export_stats && args.chunked,
                                                        .verbose = args.verbose};
        compressedSegmentationVolume = CompSegVolHandler::createCompressedSegmentationVolume(args.input_file,
                                                                                             complete_csgv_path, cfg);

        if (use_temporary_output_file) {
            if (std::filesystem::exists(complete_csgv_path))
                std::filesystem::remove(complete_csgv_path);
        }

        // if no config file was specified, use a config file at the export or import location if it exists
        if (args.rendering_configs.empty() && !use_temporary_output_file) {
            std::string config_path = stripFileExtension(complete_csgv_path) + ".vcfg";
            if (std::filesystem::exists(config_path))
                args.rendering_configs = {config_path};
            else {
                config_path = stripFileExtension(args.input_file) + ".vcfg";
                if (std::filesystem::exists(config_path))
                    args.rendering_configs = {config_path};
            }
        }
    }
    // otherwise, we load a previously compressed volume
    else {
        compressedSegmentationVolume = std::make_shared<CompressedSegmentationVolume>();
        if (!compressedSegmentationVolume->importFromFile(args.input_file, args.verbose)) {
            Logger(Error) << "could not load Compressed Segmentation Volume. Aborting.";
            return RET_COMPR_ERROR;
        }

        // try to load a precomputed database
        csgvDatabase = std::make_shared<CSGVDatabase>();
        std::string database_path = stripFileExtension(args.input_file) + "_csgv.db3";
        if (std::filesystem::exists(database_path)) {
            MiniTimer t;
            csgvDatabase->importFromSqlite(database_path);
            if (args.verbose)
                Logger(Debug) << "Imported attribute database " << database_path << " in " << t.elapsed() << " seconds";
        } else {
            csgvDatabase->createDummy();
            if (args.verbose)
                Logger(Debug) << "No attribute database " << database_path << " found. Using dummy database.";
        }

        if (args.verbose) {
            Logger(Debug) << compressedSegmentationVolume->getEncodingInfoString();
        }

        // if no config file was specified, use a previous config next to the volume input or .csgv file, if it exists
        if (args.rendering_configs.empty()) {
            std::string config_path = stripFileExtension(args.input_file) + ".vcfg";
            if (std::filesystem::exists(config_path))
                args.rendering_configs = {config_path};
        }
    }

    if (compressedSegmentationVolume == nullptr) {
        Logger(Error) << "could not create or load Compressed Segmentation Volume. Aborting.";
        return RET_COMPR_ERROR;
    }

    return RET_SUCCESS;
}

} // namespace volcanite
