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

#include <string>
#include <memory>
#include "vvv/util/Logger.hpp"
#include "vvvwindow/entrypoint.hpp"

#include "vvv/volren/Volume.hpp"
#include "vvvwindow/App.hpp"

#include "volcanite/compression/CompSegVolHandler.hpp"
#include "volcanite/compression/CompressedSegmentationVolume.hpp"

#include "related_work/cpp-compresso.h"
#include "related_work/cpp-neuroglancer.h"

using namespace vvv;
using namespace volcanite;


int eval_related_work(int argc, char *argv[]) {
    // configuration -------------------
    std::string path = "/home/maxpio/data/segmented_volumes/mouse_cortex/mapped/chunks/x0y5z0.hdf5";

    // build with cmake --build /home/maxpio/code/vvv/cmake-build-release --target eval_related_work -j 12
    // ---------------------------------

    if(argc > 1) {
        path = std::string(argv[1]);
    } else {
        Logger(Warn) << " add a path to a segmentation volume as a command line argument!";
    }

    double seconds, lzma_seconds;
    float cr, cr_lzma;

    Logger(Info) << "Evaluation volume " << path << "\n\n";
    Logger::s_minLevel = WARN; // disable all distracting messages

    // compresso
//    Logger(Info) << "Compresso ------------------------------------------------------------------";
//    {
//        //std::cout << "test " << (compresso::test(path, 1, 8, 8, compresso::LZMA) ? "ok" : "error!");
//        compresso::Compress(path, 1, 8, 8, compresso::LZMA, cr, cr_lzma, seconds, lzma_seconds);
//        std::cout << "Compresso      Compression rate: " << std::fixed << std::setprecision(9) << (cr * 100.) << "% in " << seconds << std::endl;
//        std::cout << "Compresso LZMA Compression rate: " << std::fixed << std::setprecision(9) << (cr_lzma * 100.) << "% in " << lzma_seconds << std::endl;
//    }

    // hdf5
    // Logger(Info) << "HDF5 ------------------------------------------------------------------";
    // {
    //     // TODO: implement a HighFive compressor in libvvv?
    // }

    // Fast Compressed Segmentation Volumes
    Logger(Info) << "CSGV (one thread) -----------------------------------------------------";
    {
        CompSegVolHandler::CSGVCompressionConfig cfg = {.brick_dim = 64,
                .encoding_mode = EncodingMode::DOUBLE_TABLE_RANS_ENC,
                .op_mask = OP_ALL,
                .random_access = false,
                .label_remapping = nullptr,
                .cpu_threads = 1,
                .use_detail_separation = false,
                .force_recompute = true,
                .chunked_input_data = false,
                .freq_subsampling = 2u,
                .run_tests = false,
                .export_stats_per_chunk = false,
                .verbose = false};
        auto compressedSegmentationVolume = CompSegVolHandler::createCompressedSegmentationVolume(path,
                                                                                                  Paths::getTempFileWithName(
                                                                                                          "eval_csgv.csgv"),
                                                                                                  cfg);
        auto eval_res = compressedSegmentationVolume->getLastEvaluationResults();
        std::cout << "CSGV rANS      Compression rate: " << std::fixed << std::setprecision(9)
                 << (eval_res.compression_rate * 100.) << "% in " << eval_res.compression_total_seconds << std::endl;
    }

    // Random Access Compressed Segmentation Volumes
    Logger(Info) << "CSGV-R (one thread) ---------------------------------------------------";
    {
        CompSegVolHandler::CSGVCompressionConfig cfg = {.brick_dim = 64,
                .encoding_mode = EncodingMode::HUFFMAN_WM_ENC,
                .op_mask = OP_ALL_WITHOUT_STOP,
                .random_access = true,
                .label_remapping = nullptr,
                .cpu_threads = 1,
                .use_detail_separation = false,
                .force_recompute = true,
                .chunked_input_data = false,
                .run_tests = false,
                .export_stats_per_chunk = false,
                .verbose = false
        };
        auto compressedSegmentationVolume = CompSegVolHandler::createCompressedSegmentationVolume(path,
                                                                                                  Paths::getTempFileWithName(
                                                                                                          "eval_csgv-r.csgv"),
                                                                                                  cfg);
        auto eval_res = compressedSegmentationVolume->getLastEvaluationResults();
        std::cout << "CSGV-R HuffWM  Compression rate: " << std::fixed << std::setprecision(9)
                  << (eval_res.compression_rate * 100.) << "% in " << eval_res.compression_total_seconds << std::endl;
    }

    // Random Access Compressed Segmentation Volumes
    CSGVCompressionEvaluationResults volume_info;
    Logger(Info) << "CSGV-R+sb (one thread) ------------------------------------------------";
    {
        CompSegVolHandler::CSGVCompressionConfig cfg = {.brick_dim = 64,
                .encoding_mode = EncodingMode::HUFFMAN_WM_ENC,
                .op_mask = OP_ALL,
                .random_access = true,
                .label_remapping = nullptr,
                .cpu_threads = 1,
                .use_detail_separation = false,
                .force_recompute = true,
                .chunked_input_data = false,
                .run_tests = false,
                .export_stats_per_chunk = false,
                .verbose = false
        };
        auto compressedSegmentationVolume = CompSegVolHandler::createCompressedSegmentationVolume(path,
                                                                                                  Paths::getTempFileWithName(
                                                                                                          "eval_csgv-r-sb.csgv"),
                                                                                                  cfg);
        auto eval_res = compressedSegmentationVolume->getLastEvaluationResults();
        std::cout << "CSGV-R HuffWM  Compression rate: " << std::fixed << std::setprecision(9)
                  << (eval_res.compression_rate * 100.) << "% in " << eval_res.compression_total_seconds << std::endl;


        volume_info = eval_res;
    }

    // neuroglancer
    Logger(Info) << "Neuroglancer ---------------------------------------------------------------";
    {
        //std::cout << "test " << (neuroglancer::test(path, 8) ? " ok" : " error!");
        neuroglancer::Compress(path, 8, cr, seconds);
        std::cout << "Neuroglancer   Compression rate: " << std::fixed << std::setprecision(9)
                  << (cr * static_cast<float>(4u / CompressedSegmentationVolume::getBytesForLabelCount(volume_info.volume_labels))) << "% in "
                  << seconds << std::endl;
    }

    std::cout << "-------------" << std::endl;
    std::cout << "Original Volume: " << (volume_info.original_volume_bytes * BYTE_TO_GB) << " GB"
              << " containing " << str(volume_info.volume_dim) << " voxels @ "
              << CompressedSegmentationVolume::getBytesForLabelCount(volume_info.volume_labels)
              << " bytes/voxel for " << volume_info.volume_labels << " labels. "
              << std::endl;

    return 0;
}

ENTRYPOINT(eval_related_work)
