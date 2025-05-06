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
    std::vector<const std::string> m_attribute_names = {"Volume", "Surface", "Center X", "Center Y", "Center Z", "Neighbor Count"};
    std::vector<std::vector<float>> m_attribute_values = {};

    // TODO: the negative neighbors are not required as all operations are symmetric:
    //       if x is added as a neighbor to y, we can directly add y as a neighbor to x as well.
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
        const int offset_loop_end = diagonal ? 14 : 6;
        // TODO: replace the vector<unordered_set> in thread_label_list with a vector<LabelAttributeTracking> for tracking more than the neighbors
        std::vector<std::vector<std::unordered_set<uint32_t>>> thread_label_list(cpu_threads, std::vector<std::unordered_set<uint32_t>>(m_neighbors_per_label.size()));

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
                                    // TODO: thread_tracking[thread_id][current_label].neighbor.contains(neighbor_label) etc..
                                    if (!thread_label_list[thread_id][current_label].contains(neighbor_label))
                                        thread_label_list[thread_id][current_label].insert(neighbor_label);

                                    // TODO: directly add current_label to the list of neighbor_label here as well.
                                    //       saves half the checks as -1 neighbors must no longer be checked
                                }

                                // TODO: compute surface tracking information for the voxel and the neighbor here as well
                            }
                        }

                        // TODO: compute other tracking information (voxel count, positions, ..) for (x,y,z) here as well
                    }
                }
            }
        }

        // gather all thread results and merge them together
        for (int thread_id = 1; thread_id < cpu_threads; thread_id++) {
            assert(thread_label_list[thread_id].size() == m_csgv->getNumberOfUniqueLabelsInVolume());
            for (int label = 0; label < thread_label_list[thread_id].size(); label++) {
                auto current_neighbor = thread_label_list[thread_id][label];
                for (auto neighbor_label : current_neighbor) {
                    if (!thread_label_list[0][label].contains(neighbor_label))
                        thread_label_list[0][label].insert(neighbor_label);
                }
            }
        }

        // write neighbor lists to m_neighbors_per_label
        for (int label = 0; label < thread_label_list[0].size(); label++) {
            auto current_neighbor = thread_label_list[0][label];
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

        // TODO: for all labels l, compute their final attributes (as listed in m_attribute_names) into m_attribute_values[l]
#pragma omp parallel for default(none) shared(m_attribute_values)
        for (int label = 0; label <= m_csgv->getMaxLabelInVolume(); label++) {
            // Volume
            // m_attribute_values[label][0] = label_tracking.voxel_count;
            // ...
            // Neighbor Count
            // m_attribute_values[label][5] = length(neighbor list);
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

    const std::vector<const std::string> &getAttributeNames() const {
        return m_attribute_names;
    }
};

} // namespace volcanite
