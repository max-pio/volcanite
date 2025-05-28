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

  private:
    struct NeighborList {
        ~NeighborList() {
            if (next)
                delete next;
        }

        uint32_t label = INVALID;
        NeighborList *next = nullptr;

        bool contains(const uint32_t element) const {
            const NeighborList *current = this;
            while (current != nullptr) {
                if (element == current->label)
                    return true;
                current = current->next;
            }
            return false;
        }

        void add(const uint32_t element) {
            if (!contains(element)) {
                NeighborList *current = this;
                while (current->next != nullptr) {
                    current = current->next;
                }
                current->next = new NeighborList{element, nullptr};
            }
        }
    };

    /// Tracking values for iteratively gathering all attributes for a single label
    struct LabelAttributeTracking {
        std::unordered_set<uint32_t> neighbors = {}; ///< all labels sharing a voxel face with a voxel with this label
        glm::uvec3 min_voxel = glm::uvec3(~0u);      ///< component-wise minimum coordinate of a voxel with this label
        glm::uvec3 max_voxel = glm::uvec3(0u);       ///< component-wise maximum coordinate of a voxel with this label
        size_t voxel_count = 0ull;                   ///< number of voxels that have this label
        size_t surface_count = 0ull;                 ///< number of faces to a different neighbor from voxels that have this label
        size_t sum_pos_x = 0u;                       ///< summed x coordinates of voxels with this label (center = sum / voxel_count)
        size_t sum_pos_y = 0u;                       ///< summed y coordinates of voxels with this label (center = sum / voxel_count)
        size_t sum_pos_z = 0u;                       ///< summed z coordinates of voxels with this label (center = sum / voxel_count)
    };

    std::shared_ptr<const CompressedSegmentationVolume> m_csgv;
    std::vector<NeighborList *> m_neighbors_per_label;
    std::vector<std::string> m_attribute_names = {"Volume", "Surface", "CenterX", "CenterY", "CenterZ", "Neighbor_Count"};
    std::vector<std::vector<float>> m_attribute_values = {m_csgv->getNumberOfUniqueLabelsInVolume(), std::vector<float>(6)};

    const int neighbor_offset[4][3] = {
        {0, 1, 0}, // right
        {1, 0, 0}, // front
        {0, 0, 1}, // top
        {1, 1, 1}, // top front right
    };

    void computeAttributesPerLabel(bool diagonal) {
        assert(m_neighbors_per_label.empty() && "Attributes were already computed.");

        // the labels in the CSGV are contiguous, i.e. occupy the interval [0, max_label).
        m_neighbors_per_label.resize(m_csgv->getNumberOfUniqueLabelsInVolume(), nullptr);

        // TODO: these computations should happen on a per-brick level (or a 2x2x2 bricks level): complete chunked volumes may not fit into RAM if decompressed.
        // replace decompressed_voxels with a std::vector<uint32_t> decompressed_bricks[2][2][2] (ZYX) per thread (!) where [0][0][0] is always the current brick and others are its direct top/right/.. neighbors
        // all computations happen for the current voxel and its top/right/left neighbor voxel that may be in another brick than [0][0][0]
        // if the "thread voxel" (x,y,z) moves to another brick, copy the re-usable bricks to the left/bottom/.. decompressed_bricks[..] and decompress the new bricks at the top/right/.. border.

        const auto decompressed_voxels = m_csgv->decompress();
        const auto volume_dim = m_csgv->getVolumeDim();

        const unsigned int cpu_threads = std::thread::hardware_concurrency();
        const int offset_loop_end = diagonal ? 4 : 3;

        std::vector<std::vector<LabelAttributeTracking>> thread_tracking(cpu_threads, std::vector<LabelAttributeTracking>(m_neighbors_per_label.size()));

        MiniTimer t;
        // compute attribute tracking information for the labels of multiple voxels in parallel
#pragma omp parallel num_threads(cpu_threads) default(shared)
        {
            unsigned int thread_id = omp_get_thread_num();
            for (uint32_t z = thread_id; z < volume_dim.z; z += cpu_threads) {
                if (z >= volume_dim.z)
                    continue;

                uint32_t current_label = INVALID;
                for (uint32_t y = 0; y < volume_dim.y; y++) {
                    for (uint32_t x = 0; x < volume_dim.x; x++) {
                        const uint32_t current_idx = voxel_pos2idx({x, y, z}, volume_dim);
                        current_label = decompressed_voxels->at(current_idx);

                        // compute neighbors of the label at voxel (x,y,z)
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
                                    // check for neighbors is symmetrical
                                    if (!thread_tracking[thread_id][current_label].neighbors.contains(neighbor_label))
                                        thread_tracking[thread_id][current_label].neighbors.insert(neighbor_label);

                                    if (!thread_tracking[thread_id][neighbor_label].neighbors.contains(current_label))
                                        thread_tracking[thread_id][neighbor_label].neighbors.insert(current_label);

                                    thread_tracking[thread_id][current_label].surface_count += 1;
                                    thread_tracking[thread_id][neighbor_label].surface_count += 1;
                                }
                            }
                        }
                        thread_tracking[thread_id][current_label].voxel_count += 1;
                        thread_tracking[thread_id][current_label].sum_pos_x += x;
                        thread_tracking[thread_id][current_label].sum_pos_y += y;
                        thread_tracking[thread_id][current_label].sum_pos_z += z;
                        thread_tracking[thread_id][current_label].min_voxel = glm::min(thread_tracking[thread_id][current_label].min_voxel, glm::uvec3(x, y, z));
                        thread_tracking[thread_id][current_label].max_voxel = glm::max(thread_tracking[thread_id][current_label].max_voxel, glm::uvec3(x, y, z));
                    }
                }
            }
        }

        // gather all thread results and merge them together
        for (int thread_id = 1; thread_id < cpu_threads; thread_id++) {
            assert(thread_tracking[thread_id].size() == m_csgv->getNumberOfUniqueLabelsInVolume());
            for (int label = 0; label < thread_tracking[thread_id].size(); label++) {
                // label tracking information
                auto current_neighbor = thread_tracking[thread_id][label].neighbors;
                for (auto neighbor_label : current_neighbor) {
                    if (!thread_tracking[0][label].neighbors.contains(neighbor_label))
                        thread_tracking[0][label].neighbors.insert(neighbor_label);
                }
                if (thread_tracking[thread_id][label].voxel_count > 0) {
                    // other tracking information
                    thread_tracking[0][label].min_voxel = glm::min(thread_tracking[0][label].min_voxel, thread_tracking[thread_id][label].min_voxel);
                    thread_tracking[0][label].max_voxel = glm::max(thread_tracking[0][label].max_voxel, thread_tracking[thread_id][label].max_voxel);
                    thread_tracking[0][label].voxel_count += thread_tracking[thread_id][label].voxel_count;
                    thread_tracking[0][label].surface_count += thread_tracking[thread_id][label].surface_count;

                    // Welford's algorithm
                    auto current_mean_pos = thread_tracking[0][label];
                    auto pos_to_add = thread_tracking[thread_id][label];

                    size_t delta0 = pos_to_add.sum_pos_x < current_mean_pos.sum_pos_x ? (current_mean_pos.sum_pos_x - pos_to_add.sum_pos_x) : (pos_to_add.sum_pos_x - current_mean_pos.sum_pos_x);
                    size_t delta1 = pos_to_add.sum_pos_y < current_mean_pos.sum_pos_y ? (current_mean_pos.sum_pos_y - pos_to_add.sum_pos_y) : (pos_to_add.sum_pos_y - current_mean_pos.sum_pos_y);
                    size_t delta2 = pos_to_add.sum_pos_z < current_mean_pos.sum_pos_z ? (current_mean_pos.sum_pos_z - pos_to_add.sum_pos_z) : (pos_to_add.sum_pos_z - current_mean_pos.sum_pos_z);
                    current_mean_pos.sum_pos_x += current_mean_pos.sum_pos_x + delta0 / (pos_to_add.voxel_count);
                    current_mean_pos.sum_pos_y += current_mean_pos.sum_pos_y + delta1 / pos_to_add.voxel_count;
                    current_mean_pos.sum_pos_z += current_mean_pos.sum_pos_z + delta2 / pos_to_add.voxel_count;
                }
            }
        }

        // write neighbor lists to m_neighbors_per_label
        for (int label = 0; label < thread_tracking[0].size(); label++) {
            auto current_neighbor = thread_tracking[0][label].neighbors;
            NeighborList *neighbor_list = nullptr;
            NeighborList *current_element = nullptr;
            for (const auto neighbor_label : current_neighbor) {
                if (current_element == nullptr) {
                    neighbor_list = new NeighborList{neighbor_label, nullptr};
                    current_element = neighbor_list;
                } else {
                    current_element->next = new NeighborList{neighbor_label, nullptr};
                    current_element = current_element->next;
                }
            }
            m_neighbors_per_label[label] = neighbor_list;
        }

#pragma omp parallel for default(none) shared(m_attribute_values, thread_tracking)
        for (int label = 0; label <= m_csgv->getMaxLabelInVolume(); label++) {
            // Volume
            m_attribute_values[label][0] = static_cast<float>(thread_tracking[0][label].voxel_count);
            // Surface
            m_attribute_values[label][1] = static_cast<float>(thread_tracking[0][label].surface_count);
            // Center X
            m_attribute_values[label][2] = static_cast<float>(thread_tracking[0][label].sum_pos_x) / thread_tracking[0][label].voxel_count;
            // Center Y
            m_attribute_values[label][3] = static_cast<float>(thread_tracking[0][label].sum_pos_y) / thread_tracking[0][label].voxel_count;
            // Center Z
            m_attribute_values[label][4] = static_cast<float>(thread_tracking[0][label].sum_pos_z) / thread_tracking[0][label].voxel_count;
            // Neighbor Count
            m_attribute_values[label][5] = static_cast<float>(thread_tracking[0][label].neighbors.size());
        }

        Logger(Debug) << "finished label attribute extraction in " << t.elapsed() << " seconds";
    }

  public:
    CSGVAttributeExtractor(const std::shared_ptr<CompressedSegmentationVolume> &csgv) : m_csgv(csgv), m_neighbors_per_label() {
        if (!csgv->hasContiguousLabels())
            throw std::runtime_error("Provided labels for compressed volume are not contiguous. Relabel volume with --relabel first.");

        computeAttributesPerLabel(false);
    }

    ~CSGVAttributeExtractor() {
        for (const auto &nl : m_neighbors_per_label)
            delete nl;
    }

    bool exportNeighborsPerLabel(const std::filesystem::path &path) const {
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

    const std::vector<std::vector<float>> &getAttributeValues() const {
        return m_attribute_values;
    }

    const std::vector<std::string> &getAttributeNames() const {
        return m_attribute_names;
    }
};

} // namespace volcanite
