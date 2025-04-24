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
    };

  private:
    std::shared_ptr<CompressedSegmentationVolume> m_csgv;
    std::vector<NeighborList *> m_neighbors_per_label;

    void computeNeighborsPerLabel(bool diagonal) {
        assert(m_neighbors_per_label->empty() && "Neighbors per label are already computed. You must call clear() before re-computation.");

        // the labels in the CSGV are contiguous, i.e. occupy the interval [0, max_label).
        m_neighbors_per_label.resize(m_csgv->getNumberOfUniqueLabelsInVolume(), nullptr);

        // TODO: compute neighbors per label
        //    only if m_neighbors_per_label.empty(): iterate over all voxels, obtain labels of top/right/back neighbor
        //    (+ optional the 4 diagonal neighbors). add them to the lists of the voxel if they are not yet in there.
    }

  public:
    CSGVAttributeExtractor(std::shared_ptr<CompressedSegmentationVolume> csgv) : m_csgv(csgv), m_neighbors_per_label() {
        // TODO: throw std::runtime_error(..) if !csgv->hasContiguousLabels()
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
        throw std::runtime_error("exportNeighborsPerLabel() not implemented yet.");

        // TODO: write neighbors per label to file. Per row: label followed by comma separated list of its neighbor labels, e.g.
        //  0, 3, 127, 572, 172
        //  1
        //  2, 3, 58
        //  3, 0
    }
}

} // namespace volcanite
