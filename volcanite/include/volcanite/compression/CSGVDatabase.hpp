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

#include "volcanite/CSGVPathUtils.hpp"
#include "volcanite/compression/CompSegVolHandler.hpp"

#include <glm/glm.hpp>

#include <memory>
#include <string>
#include <vector>

// forward decl
namespace SQLite {
class Database;
}

namespace volcanite {

class CSGVDatabase {

  private:
    const std::string CSGV_ATTRIBUTE_TABLE = "csgv_attribute";
    const std::string CSGV_INFO_TABLE = "csgv_info";
    const std::string ID_COLUMN = "csgv_id";
    const std::string IN_MEMORY_TABLE = "csgv_mem";

    /// Exports preprocessing results to a new database after which it is opened in read mode.
    bool databaseExportAndOpen(const std::string &sqlite_path, const std::vector<uint32_t> &index_to_label,
                               glm::uvec3 volume_dimension, glm::uvec3 chunk_dimension,
                               const std::string &attribute_database, std::string attribute_table,
                               const std::string &attribute_csv_separator, std::string label_column);

  public:
    CSGVDatabase() { createDummy(); };
    ~CSGVDatabase() { close(); }

    void close();

    /// This database will not contain any real information but will return a label count of uint32_MAX and a single
    /// attribute name "csgv_id". This way it can be used in the csgv renderer which will implicitly map this single
    /// attribute to the voxel labels from the csgv volume.
    void createDummy() {
        close();
        m_db = nullptr;
        // TODO: could create an in-memory database if we need more dummy functionality
        //  m_db = new SQLite::Database(":memory:", SQLite::OPEN_MEMORY);
        m_label_count = ~0u; // uint32 Max TODO: find the maximum palette label within a volume input file
        m_attribute_names = {"csgv_id"};
        m_attribute_minmax = {glm::vec2(0.f, static_cast<float>(m_label_count))};
    }

    [[nodiscard]] bool isDummy() const { return !m_attribute_names.empty() && (m_db == nullptr); }

    /// Updates the min / max values of the csgv_id dummy attribute, i.e. the volume labels, from the given volume.
    void updateDummyMinMax(const CompressedSegmentationVolume &csgv) {
        uint32_t min_id = ~0u;
        uint32_t max_id = 0u;
        const size_t brick_idx_count = csgv.getBrickIndexCount();

#pragma omp parallel for default(none) shared(csgv, brick_idx_count) reduction(min : min_id) reduction(max : max_id)
        for (size_t brick_idx = 0; brick_idx < brick_idx_count; brick_idx++) {
            for (const uint32_t &l : csgv.getBrickReversePalette(brick_idx)) {
                if (l < min_id)
                    min_id = l;
                if (l > max_id)
                    max_id = l;
            }
        }
        m_attribute_minmax.at(0) = {static_cast<float>(min_id), static_cast<float>(max_id)};
    }

    /// If a precomputed CSGV database exists already, it is openend.
    /// If not, the given (possibly chunked) volume at input_path is preprocessed and the result is stored in a new database.
    /// In that case, either all three or none of the attribute_* parameters must be provided.
    /// If they are provided, the label attributes for the CSGV database are imported from the given
    /// attribute_table in the attribute_database and the attribute_label is used as the key column for voxel labels in the volume file.
    void importOrProcessChunkedVolume(const std::string &volume_input_path, const std::string &sqlite_output_path,
                                      const std::string &attribute_database = "", const std::string &attribute_table = "", const std::string &attribute_label = "",
                                      const std::string &attribute_csv_separator = "",
                                      const bool chunked_input_data = false, const glm::uvec3 max_file_index = glm::uvec3(0u)) {
        if (!std::filesystem::exists(sqlite_output_path)) {
            processVolumeAndCreateSqlite(sqlite_output_path, volume_input_path,
                                         attribute_database, attribute_table, attribute_label, attribute_csv_separator,
                                         chunked_input_data, max_file_index);
        } else {
            importFromSqlite(sqlite_output_path);
        }
    }

    void importFromSqlite(const std::string &sqlite_path);

    /// For a (possibly chunked) volume, the following preprocessing is carried out and exported to a new database:\n
    /// total number of voxels in the volume,\n
    /// the size of the (0,0,0) chunk and other (inner) chunks match this size\n
    /// the number of labels and the label to index re-mapping
    void processVolumeAndCreateSqlite(const std::string &sqlite_export_path, const std::string &volume_input_path,
                                      const std::string &attribute_database, const std::string &attribute_table,
                                      const std::string &label_column, const std::string &attribute_csv_separator,
                                      bool chunked_input_data = false, glm::uvec3 max_file_index = glm::uvec3(0u));

    /// Returns a mapping of the original volume's labels to new voxel ids that are\n
    /// (1) one continuous space, i.e. [0, N) for N unique labels in the volume\n
    /// (2) ordered along a Morton Z-Curve by their first appearance in the volume
    [[nodiscard]] std::shared_ptr<std::unordered_map<uint32_t, uint32_t>> getLabelRemapping() const;

    size_t getAttributeCount() const {
        return m_attribute_names.size();
    }

    const std::vector<std::string> &getAttributeNames() const {
        return m_attribute_names;
    }

    const std::vector<glm::vec2> &getAttributeMinMax() const {
        return m_attribute_minmax;
    }

    size_t getLabelCount() const {
        return m_label_count;
    }

    /// Fills the memory area with the float attribute for the given attribute index. The buffer must be large enough
    /// to fit getLabelCount() elements. If maxSize > getLabelCount(), only getLabelCount() elements are written.
    /// @return the number of written elements
    size_t getAttribute(int attributeIndex, float *begin, size_t maxSize);

  private:
    SQLite::Database *m_db = nullptr; // sqlite database
    std::vector<std::string> m_attribute_names = {};
    std::vector<glm::vec2> m_attribute_minmax = {};
    size_t m_label_count = 0;
};

} // namespace volcanite
