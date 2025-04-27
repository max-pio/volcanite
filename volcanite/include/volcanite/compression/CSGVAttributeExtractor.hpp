//  Copyright (C) 2024, Fabian Schiekel, Max Piochowiak, Karlsruhe Institute of Technology
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

#include <memory>

#include "csgv_constants.incl"
#include "fmt/chrono.h"
#include "volcanite/compression/CompressedSegmentationVolume.hpp"
#include "volcanite/compression/memory_mapping.hpp"

using namespace vvv;

namespace volcanite {

class CSGVAttributeExtractor {

    struct NeighborList {
        ~NeighborList() {
            if (next)
                delete next;
        }

        uint32_t label = INVALID;
        NeighborList *next = nullptr;

        bool contains(const uint32_t element) const {
            const NeighborList* current = this;
            while (current != nullptr) {
                if (element == current->label)
                    return true;
                current = current->next;
            }
            return false;
        }

        void add(const uint32_t element) {
            if (!contains(element)) {
                NeighborList* current = this;
                while (current->next != nullptr) {
                    current = current->next;
                }
                current->next = new NeighborList{element, nullptr};
            }
        }
    };

  private:
    std::shared_ptr<CompressedSegmentationVolume> m_csgv;
    std::vector<NeighborList *> m_neighbors_per_label;

    const int neighbor_offset[14][3] = {
        {0, -1, 0},   // left
        {0, 1, 0},    // right
        {-1, 0, 0},   // behind
        {1, 0, 0},    // front
        {0, 0, -1},   // bottom
        {0, 0, 1},    // top
        {-1, -1, -1}, // bottom behind left
        {-1, 1, -1},  // bottom behind right
        {-1, -1, 1},  // top behind left
        {-1, 1, 1},   // top behind right
        {1, -1, -1},  // bottom front left
        {1, 1, -1},   // bottom front right
        {1, -1, 1},   // top front left
        {1, 1, 1},    // top front right
    };

    void computeNeighborsPerLabel(bool diagonal) {
        assert(m_neighbors_per_label.empty() && "Neighbors per label are already computed. You must call clear() before re-computation.");

        // the labels in the CSGV are contiguous, i.e. occupy the interval [0, max_label).
        m_neighbors_per_label.resize(m_csgv->getNumberOfUniqueLabelsInVolume(), nullptr);

        auto decompressed_voxels = m_csgv->decompress();
        auto volume_dim = m_csgv->getVolumeDim();

        unsigned int cpu_threads = std::thread::hardware_concurrency();
        int offset_loop_end = diagonal ? 14 : 6;
        std::vector<std::vector<std::unordered_set<uint32_t>>> thread_label_list(cpu_threads, std::vector<std::unordered_set<uint32_t> >(m_neighbors_per_label.size()));

        MiniTimer t;
        // compute neighbor labels in parallel
#pragma omp parallel num_threads(cpu_threads) default(shared)
        {
            unsigned int thread_id = omp_get_thread_num();
            for (uint32_t z = thread_id; z < volume_dim.z; z+=cpu_threads) {
                if (z >= volume_dim.z) continue;

                uint32_t current_label = -1;
                for (uint32_t y = 0; y < volume_dim.y; y++) {
                    for (uint32_t x = 0; x < volume_dim.x; x++) {
                        const uint32_t current_idx = voxel_pos2idx({x, y, z}, volume_dim);
                        current_label = decompressed_voxels->at(current_idx);

                        for (int i = 0; i < offset_loop_end; i++) {
                            auto offset = neighbor_offset[i];
                            int64_t nx = static_cast<int64_t>(x) + offset[0];
                            int64_t ny = static_cast<int64_t>(y) + offset[1];
                            int64_t nz = static_cast<int64_t>(z) + offset[2];

                            if (nx >= 0 && nx < volume_dim.x &&
                                ny >= 0 && ny < volume_dim.y &&
                                nz >= 0 && nz < volume_dim.z) {
                                uint32_t neighbor_label;
                                uint32_t neighbor_idx = voxel_pos2idx({nx, ny, nz}, volume_dim);
                                if ((neighbor_label = decompressed_voxels->at(neighbor_idx)) != current_label) {
                                    if (!thread_label_list[thread_id][current_label].contains(neighbor_label))
                                        thread_label_list[thread_id][current_label].insert(neighbor_label);
                                }
                            }
                        }
                    }
                }
            }
        }

        // gather all thread results and merge them together
        for (int thread_id = 1; thread_id < cpu_threads; thread_id++) {
            assert(thread_label_list[thread_id].size() == m_csgv->getNumberOfUniqueLabelsInVolume() && "shouldnt happen!");
            for (int label = 0; label < thread_label_list[thread_id].size(); label++) {
                auto current_neighbor = thread_label_list[thread_id][label];
                for (auto neighbor_label : current_neighbor) {
                    if (!thread_label_list[0][label].contains(neighbor_label))
                        thread_label_list[0][label].insert(neighbor_label);
                }
            }
        }

        // write result in m_neighbors_per_label
        for (int label = 0; label < thread_label_list[0].size(); label++) {
            auto current_neighbor = thread_label_list[0][label];
            NeighborList* neighbor_list = nullptr;
            NeighborList* current_element;
            for (auto neighbor_label : current_neighbor) {
                if (neighbor_list == nullptr) {
                    neighbor_list = new NeighborList{neighbor_label, nullptr};
                    current_element = neighbor_list;
                }
                else {
                    current_element->next = new NeighborList{neighbor_label, nullptr};
                    current_element = current_element->next;
                }
            }
            m_neighbors_per_label[label] = neighbor_list;
        }

        Logger(Debug) << "time for label extraction in " << t.elapsed() << " seconds";
    }

  public:
    CSGVAttributeExtractor(const std::shared_ptr<CompressedSegmentationVolume> &csgv) : m_csgv(csgv), m_neighbors_per_label() {
        if (!csgv->hasContiguousLabels())
            throw std::runtime_error("Provided labels for compressed volume are not contiguous. Relabel volume with --relabel first.");
        computeNeighborsPerLabel(false);
    }

    ~CSGVAttributeExtractor() {
        clear();
    }

    void clear() {
        for (auto &nl : m_neighbors_per_label)
            delete nl;
        m_neighbors_per_label = {};
    }

    bool exportNeighborsPerLabel(const std::filesystem::path &path) {
        std::ofstream outFile(path);
        if (!outFile) {
            Logger(Warn) << "Could not export neighbors per label. Check if path is valid.";
            return false;
        }

        for (int i = 0; i < m_neighbors_per_label.size(); i++) {
            outFile << i;
            auto element = m_neighbors_per_label[i];
            while (element != nullptr) {
                outFile << ", " << element->label;
                element = element->next;
            }
            outFile << std::endl;
        }
        outFile.close();

        return true;
    }
};

} // namespace volcanite
