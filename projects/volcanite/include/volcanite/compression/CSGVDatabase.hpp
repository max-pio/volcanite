#pragma once

#include "vvv/volren/Volume.hpp"
#include "vvv/util/space_filling_curves.hpp"

#include "volcanite/compression/CompSegVolHandler.hpp"
#include "SQLiteCpp/VariadicBind.h"

#include <glm/glm.hpp>

#include <memory>
#include <string>
#include <utility>
#include <vector>

#ifdef LIB_SQLITE3
#include <SQLiteCpp/SQLiteCpp.h>
#include <SQLiteCpp/VariadicBind.h>
#endif

namespace vvv {

class CSGVDatabase {

private:
    const std::string ATTRIBUTE_TABLE = "csgv_attribute";
    const std::string INFO_TABLE = "csgv_info";
    const std::string ID_COLUMN = "csgv_id";
    const std::string LABEL_COLUMN = "csgv_orig_label";

    /**
     * Exports preprocessing results to a new database after which it is opened in read mode.
     */
    bool databaseExportAndOpen(const std::string& sqlite_path, const std::vector<uint32_t>& m_index_to_label) {
        if(m_db) {
            Logger(WARN) << "closing existing csgv database " << m_db->getFilename() << " before creation";
            close();
        }

        SQLite::Database db(sqlite_path, SQLite::OPEN_READWRITE | SQLite::OPEN_CREATE);
        // store general volume info
        {
            db.exec("CREATE TABLE " + INFO_TABLE + " (volume_width INTEGER, volume_height INTEGER, volume_depth INTEGER, chunk_width INTEGER, chunk_height INTEGER, chunk_depth INTEGER)");
            SQLite::Statement query(db, "INSERT INTO " + INFO_TABLE + " VALUES (?, ?, ?, ?, ?, ?)");
            SQLite::bind(query, m_volume_dimension[0], m_volume_dimension[1], m_volume_dimension[2], m_chunk_dimension[0], m_chunk_dimension[1], m_chunk_dimension[2]);
            if(query.exec() != 1)
                Logger(WARN) << "Could not export " + INFO_TABLE + " to sqlite";
        }

        // store mapping of original volume label <-> our packed csgv ids
        db.exec("CREATE TABLE " + ATTRIBUTE_TABLE + " (" + ID_COLUMN + " INTEGER PRIMARY KEY, " + LABEL_COLUMN + " INTEGER UNIQUE)");

        SQLite::Transaction transaction(db);
        SQLite::Statement query(db, "INSERT INTO " + ATTRIBUTE_TABLE + " VALUES (?, ?)");
        for(uint32_t i = 0u; i < m_index_to_label.size(); i++) {
            SQLite::bind(query, i, m_index_to_label[i]);
            if(query.exec() != 1)
                Logger(WARN) << "Could not insert entry for label into sqlite database";
            query.reset();
        }
        transaction.commit();

        // ToDo: Join with existing sqlite attribute database

        // reimport database as read only
        m_db = std::make_unique<SQLite::Database>(sqlite_path, SQLite::OPEN_READONLY);
        return true;
    }

public:
    CSGVDatabase() = default;
    ~CSGVDatabase() { close(); }

    void close() {
        m_db = nullptr;
    }

    /** If a precomputed CSGV database exists already, it is openend.
     *  If not, the given (possibly chunked) volume at input_path is preprocessed and the result is stored in a new database.
     */
    void importOrProcessChunkedVolume(const std::string& volume_input_path, const std::string& sqlite_path, bool chunked_input_data = false, glm::uvec3 max_file_index = glm::uvec3(0u)) {
        if(!std::filesystem::exists(sqlite_path)) {
            processVolumeAndCreateSqlite(sqlite_path, volume_input_path, chunked_input_data, max_file_index);
        }
        else {
            importFromSqlite(sqlite_path);
        }
    }

    void importFromSqlite(const std::string& sqlite_path) {
        m_db = std::make_unique<SQLite::Database>(sqlite_path, SQLite::OPEN_READONLY);
    }

    /** For a (possibly chunked) volume, the following preprocessing is carried out and exported to a new database:\n
     * 1. total number of voxels in the volume and the size of the (0,0,0) chunk\n
     * 2.
     */
    void processVolumeAndCreateSqlite(const std::string& sqlite_export_path, const std::string& volume_input_path, bool chunked_input_data = false, glm::uvec3 max_file_index = glm::uvec3(0u)) {
        std::shared_ptr<Volume<uint32_t>> volume = nullptr;
        std::unordered_set<uint32_t> label_set = {};    // hash set to speed up the {label already exists} check
        std::vector<uint32_t> index_to_label = {};

        // iterate over all chunk files in morton order
        bool chunk_dimensions_vary = false;
        size_t chunk_index1D = 0ul;
        glm::uvec3 chunk_index(0ul);
        do {
            chunk_index = sfc::Morton3D::i2p(chunk_index1D);
            if(glm::all(glm::lessThanEqual(chunk_index, max_file_index))) {
                    // create file input path for this single chunk
                    std::string chunk_input_path = chunked_input_data ? CompSegVolHandler::formatChunkPath(volume_input_path,
                                                                                                           static_cast<int>(chunk_index.x),
                                                                                                           static_cast<int>(chunk_index.y),
                                                                                                           static_cast<int>(chunk_index.z))
                                                                      : volume_input_path;
                    // load chunk volume
                    Logger(DEBUG) << "Label preprocessing " << chunk_input_path;
                    CompSegVolHandler::loadSegmentationVolumeFile(chunk_input_path, volume);
                    glm::uvec3 cur_chunk_dim(volume->dim_x, volume->dim_y, volume->dim_z);

                    // process chunk volume
                    if(chunk_index1D == 0ul) {
                        m_chunk_dimension[0] = cur_chunk_dim.x;
                        m_chunk_dimension[1] = cur_chunk_dim.y;
                        m_chunk_dimension[2] = cur_chunk_dim.z;
                    } else if(m_chunk_dimension[0] != cur_chunk_dim.x
                           || m_chunk_dimension[1] != cur_chunk_dim.y
                           || m_chunk_dimension[2] != cur_chunk_dim.z) {
                        chunk_dimensions_vary = true;
                    }

                    // update tracking information
                    m_volume_dimension[0] += cur_chunk_dim.x;
                    m_volume_dimension[1] += cur_chunk_dim.y;
                    m_volume_dimension[2] += cur_chunk_dim.z;

                    // process chunk: iterate over all voxels in morton order, add them to the existing_labels set and
                    // the index_to_label vector if they did not occur before.
                    {
                        // iterate over all chunk voxels in morton order
                        size_t i = 0ul;
                        glm::uvec3 voxel(0ul);

                        // ToDo: parallelize with OpenMP?
                        do {
                            if(glm::all(glm::lessThan(voxel, cur_chunk_dim))) {
                                uint32_t label = volume->getElement(voxel);
                                if (!label_set.contains(label)) {
                                    index_to_label.push_back(label);
                                    label_set.insert(label);
                                }
                            }
                            i++;
                            voxel = sfc::Morton3D::i2p(i);
                        } while(glm::any(glm::lessThan(voxel, cur_chunk_dim)));

                        if(index_to_label.size() != label_set.size())
                            throw std::runtime_error("existing_labels set does not match index_to_label size in volume label occurrence processing");
                    }
            }

            chunk_index1D++;
        } while(chunked_input_data && glm::any(glm::lessThanEqual(chunk_index, max_file_index)));

        if(chunk_dimensions_vary)
            Logger(WARN) << "chunk dimensions vary and can differ from expected dimension " << str(glm::uvec3(m_chunk_dimension[0], m_chunk_dimension[1], m_chunk_dimension[2]));

        // create new SQLite database, export all data and then re-import as read only
        databaseExportAndOpen(sqlite_export_path, index_to_label);
    }

    /**
     * Returns a mapping of the original volume's labels to new voxel ids that are\n
     * (1) one continuous space, i.e. [0, N) for N unique labels in the volume\n
     * (2) ordered along a Morton Z-Curve by their first appearance in the volume
     */
    [[nodiscard]] std::shared_ptr<std::unordered_map<uint32_t, uint32_t>> getLabelRemapping() const {
        if(!m_db)
            throw std::runtime_error("No CSGV sqlite database present.");

        // clear the label_to_index map and reserve memory
        uint32_t columns = m_db->execAndGet("SELECT COUNT(*) FROM " + ATTRIBUTE_TABLE).getUInt();
        auto label_to_index = std::make_shared<std::unordered_map<uint32_t, uint32_t>>();
        label_to_index->reserve(columns);

        // fill the map with entries for all labels
        SQLite::Statement query(*m_db, "SELECT " + ID_COLUMN + ", " + LABEL_COLUMN + " FROM " + ATTRIBUTE_TABLE);
        const int id_column = query.getColumnIndex(ID_COLUMN.c_str());
        const int label_column = query.getColumnIndex(LABEL_COLUMN.c_str());
        while (query.executeStep())
            (*label_to_index)[query.getColumn(label_column)] = query.getColumn(id_column);

        return label_to_index;
    }

private:
    std::unique_ptr<SQLite::Database> m_db = nullptr;   // sqlite database
    uint32_t m_volume_dimension[3] = {0u, 0u, 0u};      // total size in voxels
    uint32_t m_chunk_dimension[3] = {0u, 0u, 0u};       // dimension of a single chunk
};


} // namespace vvv
