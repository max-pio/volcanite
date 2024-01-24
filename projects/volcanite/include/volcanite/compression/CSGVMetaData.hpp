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

class CSGVMetaData {
private:
    const std::string CURRENT_MCSV_HEADER = "MCSV0";
public:
    CSGVMetaData() = default;

    bool importFromSqlite(const std::string& sqlite_path) {
        MiniTimer t;
        SQLite::Database db(sqlite_path, SQLite::OPEN_READONLY);

        // clear the label_to_index map and reserve memory
        uint32_t columns = db.execAndGet("SELECT COUNT(*) FROM volcanite").getUInt();
        m_label_to_index.clear();
        m_label_to_index.reserve(columns);

        // fill the map with entries for all labels
        SQLite::Statement query(db, "SELECT volcanite_id label FROM volcanite");
        const int id_column = query.getColumnIndex("volcanite_id");
        const int label_column = query.getColumnIndex("label");
        while (query.executeStep())
            m_label_to_index[query.getColumn(label_column)] = query.getColumn(id_column);

        Logger(DEBUG) << "imported data from sqlite " << db.getFilename() << " in " << t.elapsed() << " seconds";
        return true;
    }

    bool exportToSqlite(const std::string& sqlite_path) {

        MiniTimer t;
        SQLite::Database db(sqlite_path, SQLite::OPEN_READWRITE | SQLite::OPEN_CREATE);

        // store general volume info
        {
            db.exec("CREATE TABLE volcanite_info (volume_width INTEGER, volume_height INTEGER, volume_depth INTEGER, chunk_width INTEGER, chunk_height INTEGER, chunk_depth INTEGER)");
            SQLite::Statement query(db, "INSERT INTO volcanite_info VALUES (?, ?, ?, ?, ?, ?)");
            SQLite::bind(query, m_volume_dimension[0], m_volume_dimension[1], m_volume_dimension[2], m_chunk_dimension[0], m_chunk_dimension[1], m_chunk_dimension[2]);
            if(query.exec() != 1) {
                Logger(WARN) << "Could not export volcanite_info to sqlite";
            }
        }

        // store mapping of original volume label <-> volcanite ids
        db.exec("CREATE TABLE volcanite (id INTEGER PRIMARY KEY, label INTEGER UNIQUE)");

        SQLite::Transaction transaction(db);
        SQLite::Statement query(db, "INSERT INTO volcanite VALUES (?, ?)");
        for(const auto& e : m_label_to_index) {
            SQLite::bind(query, e.second, e.first); // volcanite_id, label
            query.exec();
            query.reset();
        }
        transaction.commit();
        Logger(DEBUG) << "exported data to sqlite " << db.getFilename() << " in " << t.elapsed() << " seconds";
        return true;
    }



    /** Iterates over all voxels in the given volume in morton order and pushes any labels that are not yet included
     * int index_to_label to index_to_label. Can be used to iteratively process multiple chunks of a single volume.
     * If the set existing_labels is provided to speed up the search, it must contain all labels from index_to_label.
     */
    static void addLabelsFromVolume(const std::shared_ptr<Volume<uint32_t>>& volume, std::unordered_map<uint32_t, uint32_t>& label_to_index, std::unordered_set<uint32_t>* existing_labels = nullptr) {
        // we use a hash set to speed up the check if a label was already processed
        std::unordered_set<uint32_t>* label_set = existing_labels;
        if(label_set == nullptr) {
            label_set = new std::unordered_set<uint32_t>();
            for(const auto& e : label_to_index) {
                label_set->insert(e.first);
            }
        }
        if(label_to_index.size() != label_set->size())
            throw std::runtime_error("existing_labels set does not match index_to_label size in volume label occurrence processing");

        // iterate over all volume voxels in morton order
        size_t i = 0ul;
        glm::uvec3 voxel(0ul);
        glm::uvec3 vol_dim(volume->dim_x, volume->dim_y, volume->dim_z);

        // ToDo: parallelize with OpenMP?
        do {
            if(glm::all(glm::lessThan(voxel, vol_dim))) {
                uint32_t label = volume->getElement(voxel);
                if (!label_set->contains(label)) {
                    label_to_index[label] = label_set->size();
                    label_set->insert(label);
                }
            }
            i++;
            voxel = sfc::Morton3D::i2p(i);
        } while(glm::any(glm::lessThan(voxel, vol_dim)));

        assert(label_to_index.size() == label_set->size() && "label_to_index and label_set must have same size");
        if(!existing_labels)
            delete label_set;
    }

    void processChunkedVolume(const std::string& input_path, bool chunked_input_data = false, glm::uvec3 max_file_index = glm::uvec3(0u)) {
        std::shared_ptr<Volume<uint32_t>> volume = nullptr;
        std::unordered_set<uint32_t> m_existing_labels = {};    // hash set to speed up the {label already exists} check
        m_label_to_index.clear();

        // iterate over all chunk files in morton order
        bool chunk_dimensions_vary = false;
        size_t chunk_index1D = 0ul;
        glm::uvec3 chunk_index(0ul);
        do {
            chunk_index = sfc::Morton3D::i2p(chunk_index1D);
            if(glm::all(glm::lessThanEqual(chunk_index, max_file_index))) {
                    // create file input path for this single chunk
                    std::string chunk_input_path = chunked_input_data ? CompSegVolHandler::formatChunkPath(input_path, chunk_index.x, chunk_index.y, chunk_index.z)
                                                                      : input_path;
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
                    addLabelsFromVolume(volume, m_label_to_index, &m_existing_labels);
            }

            chunk_index1D++;
        } while(chunked_input_data && glm::any(glm::lessThanEqual(chunk_index, max_file_index)));

        if(chunk_dimensions_vary)
            Logger(WARN) << "chunk dimensions vary and can differ from expected dimension " << str(glm::uvec3(m_chunk_dimension[0], m_chunk_dimension[1], m_chunk_dimension[2]));
    }

    /** Tries to load a precomputed sqlite file for the given volume.
     *  If it does not exists, the sqlite file is created after preprocessing the volume instead.
     */
    void importOrProcessChunkedVolume(const std::string& input_path, const std::string& sqlite_path, bool chunked_input_data = false, glm::uvec3 max_file_index = glm::uvec3(0u)) {
        if(!std::filesystem::exists(sqlite_path)) {
            processChunkedVolume(input_path, chunked_input_data, max_file_index);
            exportToSqlite(sqlite_path);
        }
        else {
            importFromSqlite(sqlite_path);
        }
    }

    inline uint32_t labelToIndex(uint32_t label) const { return m_label_to_index.at(label); }

private:
    std::unordered_map<uint32_t, uint32_t> m_label_to_index = {};   // map original file's labels to volcanite's voxel ids
    uint32_t m_volume_dimension[3] = {0u, 0u, 0u};                  // total size in voxels
    uint32_t m_chunk_dimension[3] = {0u, 0u, 0u};                   // dimension of a single chunk
};


} // namespace vvv
