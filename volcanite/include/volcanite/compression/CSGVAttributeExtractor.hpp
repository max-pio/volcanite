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

    struct ThreadBlock {
        unsigned int start_x;
        unsigned int end_x;
        unsigned int start_y;
        unsigned int end_y;
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
    std::vector<NeighborList *> m_thread_access;
    std::vector<std::string> m_attribute_names = {"Volume", "Surface", "CenterX", "CenterY", "CenterZ", "Neighbor_Count"};
    std::vector<std::vector<float>> m_attribute_values = {m_csgv->getNumberOfUniqueLabelsInVolume(), std::vector<float>(6)};

    const int neighbor_offset[4][3] = {
        {0, 1, 0}, // right
        {1, 0, 0}, // front
        {0, 0, 1}, // top
        {1, 1, 1}, // top front right
    };

    /// returns label for given position
    static uint32_t getLabelFromBrick(const glm::uvec3 &global_pos, const uint32_t brick_size, const glm::uvec3 start_current_brick, const std::vector<uint32_t> decompressed_bricks[2][2][2]) {
        // global_pos in (XYZ), brick_pos in (XYZ), decompressed_bricks in (ZYX),
        glm::uvec3 brick_idx = {global_pos.x >= brick_size * start_current_brick.x + brick_size ? 1 : 0,
                                global_pos.y >= brick_size * start_current_brick.y + brick_size ? 1 : 0,
                                global_pos.z >= brick_size * start_current_brick.z + brick_size ? 1 : 0};
        const uint32_t idx = voxel_pos2idx({global_pos.x % brick_size, global_pos.y % brick_size, global_pos.z % brick_size}, {brick_size, brick_size, brick_size});
        return decompressed_bricks[brick_idx[2]][brick_idx[1]][brick_idx[0]][idx];
    }

    void computeAttributesPerLabel(bool diagonal) {
        assert(m_neighbors_per_label.empty() && "Attributes were already computed.");

        const auto volume_dim = m_csgv->getVolumeDim();
        const uint32_t brick_size = m_csgv->getBrickSize();

        // the labels in the CSGV are contiguous, i.e. occupy the interval [0, max_label).
        m_neighbors_per_label.resize(m_csgv->getNumberOfUniqueLabelsInVolume(), nullptr);
        m_thread_access.resize(volume_dim.x * volume_dim.y * volume_dim.z, nullptr);

        double preliminary_cpu_threads = glm::min(std::thread::hardware_concurrency(), volume_dim.z);
        // round cpu threads to nearest square -> for parallelization
        const unsigned int cpu_threads = glm::max(static_cast<int>(std::pow(std::ceil(std::sqrt(preliminary_cpu_threads)), 2)), 2);
        const int offset_loop_end = diagonal ? 4 : 3;

        std::vector<std::vector<LabelAttributeTracking>> thread_tracking(cpu_threads, std::vector<LabelAttributeTracking>(m_neighbors_per_label.size()));
        std::vector<ThreadBlock> thread_blocks;
        std::vector<std::vector<LabelAttributeTracking>> volume_thread_debug_count (cpu_threads, std::vector<LabelAttributeTracking>( volume_dim.x * volume_dim.y * volume_dim.z));

        MiniTimer t;
        // divide volume for parallelization
        {
            const int cpu_threads_sqrt = static_cast<int>(std::sqrt(cpu_threads));
            const unsigned int x_per_thread = volume_dim.x / cpu_threads_sqrt;
            const unsigned int x_remainder = volume_dim.x % cpu_threads_sqrt;
            const unsigned int y_per_thread = volume_dim.y / cpu_threads_sqrt;
            const unsigned int y_remainder = volume_dim.y % cpu_threads_sqrt;

            unsigned int current_x = 0;
            unsigned int current_y = 0;

            for (int threads_y_id = 0; threads_y_id < cpu_threads_sqrt; threads_y_id++) {
                unsigned int y_start = current_y;
                unsigned int y_size = y_per_thread + (threads_y_id < y_remainder ? 1 : 0);
                unsigned int y_end = y_start + y_size;
                current_x = 0;
                for (int threads_x_id = 0; threads_x_id < cpu_threads_sqrt; threads_x_id++) {
                    unsigned int x_start = current_x;
                    unsigned int x_size = x_per_thread + (threads_x_id < x_remainder ? 1 : 0);
                    unsigned int x_end = x_start + x_size;

                    ThreadBlock tb = {x_start, x_end, y_start, y_end};
                    thread_blocks.emplace_back(tb);

                    current_x = x_end;
                }
                current_y = y_end;
            }
        }

            // compute attribute tracking information for the labels of multiple voxels in parallel
#pragma omp parallel num_threads(cpu_threads) default(shared)
        {
            // parallelization done by dividing the xy-plane between threads and iterating through each xy-brick-part of every thread, bricks can be divided between different threads
            // z-axis is iterated through for every xy-brick-part

            glm::uvec3 start_current_brick(0); // (XYZ)
            /* 0,0,0 is current
             * 1,0,0 is top
             * 0,1,0 is right
             * 0,0,1 is front
             * 1,1,1 is diag
             */
            std::vector<uint32_t> decompressed_bricks[2][2][2]; // (ZYX)
            bool init_decompression = true;
            unsigned int thread_id = omp_get_thread_num();
            ThreadBlock tb = thread_blocks[thread_id];

            // determine how many individual blocks need to be iterated through
            uint32_t first_block_x = tb.start_x / brick_size;
            uint32_t last_block_x = (tb.end_x - 1) / brick_size;
            uint32_t first_block_y = tb.start_y / brick_size;
            uint32_t last_block_y = (tb.end_y - 1) / brick_size;

            uint32_t count_blocks_x = last_block_x - first_block_x + 1;
            uint32_t count_blocks_y = last_block_y - first_block_y + 1;

            // calculate starting offset after brick one
            glm::uvec2 elements_in_first_block;
            elements_in_first_block.x = brick_size - (tb.start_x % brick_size);
            elements_in_first_block.y = brick_size - (tb.start_y % brick_size);
            glm::uvec2 start_offset (0);
            for (uint32_t thread_block_y = 0; thread_block_y < count_blocks_y; thread_block_y++) {
                if (thread_block_y != 0)
                    // increment start_offset after first block
                    start_offset.y += (thread_block_y == 1 ? elements_in_first_block.y : brick_size);
                start_offset.x = 0;
                for (uint32_t thread_block_x = 0; thread_block_x < count_blocks_x; thread_block_x++) {
                    if (thread_block_x != 0)
                        start_offset.x += thread_block_x == 1 ? elements_in_first_block.x : brick_size;
                    for (uint32_t z = 0; z < volume_dim.z; z ++) {
                        uint32_t current_label = INVALID;

                        // check if new xy-plane is in new brick and decompress new bricks if needed
                        {
                            glm::uvec3 start_new_brick = {(tb.start_x + start_offset.x) / brick_size,
                                                          (tb.start_y + start_offset.y) / brick_size,
                                                          z / brick_size};

                            if (start_current_brick != start_new_brick | init_decompression) {
                                std::vector<glm::uvec3> bricks_to_decompress_offset; // offsets of brick that can't be reused and needs to be decompressed (ZYX)
                                if (init_decompression || glm::any(glm::greaterThan(start_current_brick, start_new_brick))) {
                                    // need to decompress all blocks again, because end of volume reached in one dimension
                                    bricks_to_decompress_offset.emplace_back(0, 0, 0);
                                    bricks_to_decompress_offset.emplace_back(0, 0, 1);
                                    bricks_to_decompress_offset.emplace_back(0, 1, 0);
                                    bricks_to_decompress_offset.emplace_back(1, 0, 0);
                                    bricks_to_decompress_offset.emplace_back(0, 1, 1);
                                    bricks_to_decompress_offset.emplace_back(1, 0, 1);
                                    bricks_to_decompress_offset.emplace_back(1, 1, 0);
                                    bricks_to_decompress_offset.emplace_back(1, 1, 1);
                                    init_decompression = false;
                                }
                                // current voxel is in another brick
                                else if (diagonal && (start_current_brick.x != start_new_brick.x && start_current_brick.y != start_new_brick.y && start_current_brick.z != start_new_brick.z)) {
                                    // copy diag brick
                                    decompressed_bricks[0][0][0] = decompressed_bricks[1][1][1];
                                    bricks_to_decompress_offset.emplace_back(0, 0, 1);
                                    bricks_to_decompress_offset.emplace_back(0, 1, 0);
                                    bricks_to_decompress_offset.emplace_back(1, 0, 0);
                                    bricks_to_decompress_offset.emplace_back(0, 1, 1);
                                    bricks_to_decompress_offset.emplace_back(1, 0, 1);
                                    bricks_to_decompress_offset.emplace_back(1, 1, 0);
                                    bricks_to_decompress_offset.emplace_back(1, 1, 1);
                                } else if (start_current_brick.z != start_new_brick.z) {
                                    // copy top block
                                    decompressed_bricks[0][0][0] = decompressed_bricks[1][0][0];
                                    decompressed_bricks[0][1][0] = decompressed_bricks[1][1][0];
                                    decompressed_bricks[0][1][1] = decompressed_bricks[1][1][1];
                                    decompressed_bricks[0][0][1] = decompressed_bricks[1][0][1];

                                    bricks_to_decompress_offset.emplace_back(1, 0, 0);
                                    bricks_to_decompress_offset.emplace_back(1, 1, 0);
                                    bricks_to_decompress_offset.emplace_back(1, 1, 1);
                                    bricks_to_decompress_offset.emplace_back(1, 0, 1);
                                }

                                // decompress new top/right/front/diag
                                for (int i = 0; i < bricks_to_decompress_offset.size(); i++) {
                                    auto offset = bricks_to_decompress_offset[i];
                                    // brick_pos_to_decompress needs to be in (XYZ) because m_csgv->decompressBrickTo expects it to be
                                    glm::uvec3 brick_pos_to_decompress = {start_new_brick[0] + offset[2],
                                                                          start_new_brick[1] + offset[1],
                                                                          start_new_brick[2] + offset[0]};
                                    if (glm::any(glm::greaterThanEqual(brick_pos_to_decompress, m_csgv->getBrickCount())))
                                        // outside of volume
                                        continue;

                                    std::vector<uint32_t> decompressed_brick(brick_size * brick_size * brick_size, INVALID);
                                    std::vector<uint32_t> tmp(brick_size * brick_size * brick_size, INVALID);
                                    m_csgv->decompressBrickTo(tmp.data(), brick_pos_to_decompress, static_cast<int>(m_csgv->getLodCountPerBrick() - 1u));
                                    // fill output array with decoded brick entries
                                    for (uint32_t j = 0; j < brick_size * brick_size * brick_size; j++) {
                                        if (glm::uvec3 out_pos = enumBrickPos(j);
                                            glm::all(glm::lessThan(out_pos, {brick_size, brick_size, brick_size}))) {
                                            decompressed_brick[brick_pos2idx(out_pos, {brick_size, brick_size, brick_size})] = tmp[j];
                                        }
                                    }
                                    decompressed_bricks[offset[0]][offset[1]][offset[2]] = decompressed_brick;
                                }
                                start_current_brick = start_new_brick;
                            }
                        }

                        // iterate over xy-brick part
                        for (uint32_t y = tb.start_y + start_offset.y; y < (thread_block_y + first_block_y + 1) * brick_size; y++) {
                            for (uint32_t x = tb.start_x + start_offset.x; x < (thread_block_x + first_block_x + 1) * brick_size; x++) {
                                if (y >= tb.end_y || x >= tb.end_x || y >= volume_dim.y || x >= volume_dim.x)
                                    continue;

                                current_label = getLabelFromBrick({x, y, z}, brick_size, start_current_brick, decompressed_bricks);

                                // compute neighbors of voxel (x,y,z)
                                for (int i = 0; i < offset_loop_end; i++) {
                                    auto offset = neighbor_offset[i];
                                    int64_t nx = static_cast<int64_t>(x) + offset[0];
                                    int64_t ny = static_cast<int64_t>(y) + offset[1];
                                    int64_t nz = static_cast<int64_t>(z) + offset[2];

                                    if (nx >= 0 && nx < volume_dim.x &&
                                        ny >= 0 && ny < volume_dim.y &&
                                        nz >= 0 && nz < volume_dim.z) {
                                        if (uint32_t neighbor_label = getLabelFromBrick({nx, ny, nz}, brick_size, start_current_brick, decompressed_bricks);
                                            neighbor_label != current_label) {
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
            }
        }

        // gather all thread results and merge them together
        for (int thread_id = 1; thread_id < cpu_threads; thread_id++) {
            assert(thread_tracking[thread_id].size() == m_csgv->getNumberOfUniqueLabelsInVolume());
            for (int label = 0; label < thread_tracking[thread_id].size(); label++) {
                // label tracking information
                for (auto current_neighbor = thread_tracking[thread_id][label].neighbors;
                    auto neighbor_label : current_neighbor) {
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
                    auto& current_mean_pos = thread_tracking[0][label];
                    const auto& pos_to_add = thread_tracking[thread_id][label];

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
