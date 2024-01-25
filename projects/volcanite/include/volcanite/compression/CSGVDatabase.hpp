#pragma once

#ifdef LIB_SQLITE3

#include "vvv/volren/Volume.hpp"
#include "vvv/util/space_filling_curves.hpp"

#include "volcanite/compression/CompSegVolHandler.hpp"
#include "SQLiteCpp/VariadicBind.h"

#include <glm/glm.hpp>

#include <memory>
#include <string>
#include <utility>
#include <vector>


#include <SQLiteCpp/SQLiteCpp.h>
#include <SQLiteCpp/VariadicBind.h>

namespace vvv {

class CSGVDatabase {

private:
    const std::string ATTRIBUTE_TABLE = "csgv_attribute";
    const std::string INFO_TABLE = "csgv_info";
    const std::string ID_COLUMN = "csgv_id";

    /**
     * Exports preprocessing results to a new database after which it is opened in read mode.
     */
    bool databaseExportAndOpen(const std::string& sqlite_path, const std::vector<uint32_t>& m_index_to_label,
                               glm::uvec3 volume_dimension, glm::uvec3 chunk_dimension,
                               const std::string& attribute_database, const std::string& attribute_table,
                               const std::string& label_column) {
        if(m_db) {
            Logger(WARN) << "closing existing csgv database " << m_db->getFilename() << " before creation";
            close();
        }

        try{
            std::string label_name = label_column.empty() ? "label" : label_column;
            SQLite::Database db(sqlite_path, SQLite::OPEN_READWRITE | SQLite::OPEN_CREATE);
            // store general volume info
            {
                db.exec("CREATE TABLE " + INFO_TABLE +
                        " (volume_width INTEGER, volume_height INTEGER, volume_depth INTEGER, chunk_width INTEGER, chunk_height INTEGER, chunk_depth INTEGER, label_column TEXT)");
                SQLite::Statement query(db, "INSERT INTO " + INFO_TABLE + " VALUES (?, ?, ?, ?, ?, ?, ?)");
                SQLite::bind(query, volume_dimension[0], volume_dimension[1], volume_dimension[2],
                             chunk_dimension[0], chunk_dimension[1], chunk_dimension[2], label_name);
                if (query.exec() != 1)
                    Logger(WARN) << "Could not export " + INFO_TABLE + " to sqlite";
            }

            // store mapping of original volume label <-> our packed csgv ids
            {
                db.exec("CREATE TABLE " + ATTRIBUTE_TABLE + " (" + ID_COLUMN + " UNSIGNED INT PRIMARY KEY, " + label_name +
                        " UNSIGNED INT UNIQUE)");

                SQLite::Transaction transaction(db);
                SQLite::Statement query(db, "INSERT INTO " + ATTRIBUTE_TABLE + " VALUES (?, ?)");
                for (uint32_t i = 0u; i < m_index_to_label.size(); i++) {
                    SQLite::bind(query, i, m_index_to_label[i]);
                    if (query.exec() != 1)
                        Logger(WARN) << "Could not insert entry for label into sqlite database";
                    query.reset();
                }
                transaction.commit();
            }

            // Add attributes from existing
            if(!attribute_database.empty()) {
                if(attribute_table.empty() || label_column.empty())
                    throw std::runtime_error("When providing an attribute database you must also provide its attribute table and label column name");

                db.exec("ATTACH DATABASE '" + attribute_database + "' AS attr_db");
                // 0. check if the provide label column only contains unique elements
                {
                    SQLite::Statement check_duplicates(db, "SELECT " + label_column + ", COUNT(*) AS cnt FROM attr_db." + attribute_table + " GROUP BY " + label_column + " HAVING cnt > 1");
                    if(check_duplicates.executeStep())
                        throw std::runtime_error("Label column " + label_column + " in " + attribute_database + "." + attribute_table + " contains forbidden duplicate entries.");
                }

                // 1. read all column names and types except the LABEL column from attribute file as a comma separated string
                std::vector<std::string> attr_col_names;
                std::vector<std::string> attr_col_types;
                {
                    SQLite::Statement column_query(db, "SELECT name, type FROM pragma_table_info('" + attribute_table +
                                                       "','attr_db') ORDER BY cid");
                    while (column_query.executeStep()) {
                        std::string c = column_query.getColumn(0).getString();
                        if (!std::equal(c.begin(), c.end(), label_column.begin(), label_column.end(),
                                        [](char a, char b) { return tolower(a) == tolower(b); })) {
                            attr_col_names.push_back(c);
                            attr_col_types.push_back(column_query.getColumn(1));
                        }
                    }
                }

                // 2. add new columns for all existing attributes to csgv attribute table, and
                // 3. add original attribute values via UPDATE to the csgv attribute table
                // ToDo: Can we speed this up with JOINs instead of UPDATEs or something?
                {
                    std::string alter_query_format = "ALTER TABLE " + ATTRIBUTE_TABLE + " ADD COLUMN ";
                    std::string update_query_format[3] = {"UPDATE " + ATTRIBUTE_TABLE + " SET ", " = (SELECT ", " FROM attr_db." + attribute_table + " WHERE " + ATTRIBUTE_TABLE + "." +
                            label_column + " = attr_db." + attribute_table + "." + label_column + ")"};
                    SQLite::Transaction transaction(db);
                    for (int i = 0; i < attr_col_names.size(); i++) {
                        // create table column
                        // example: ALTER TABLE csgv_attribute ADD COLUMN volume REAL"
                        db.exec(alter_query_format + attr_col_names[i] + " " + attr_col_types[i]);
                        // fill table column with data
                        // example: UPDATE csgv_attribute SET volume = (SELECT volume FROM attr_db.cells WHERE csgv_attribute.label = attr_db.cells.label)
                        db.exec(update_query_format[0] + attr_col_names[i] + update_query_format[1] + attr_col_names[i] + update_query_format[2]);
                    }
                    transaction.commit();
                }
            }

            db.exec("DETACH DATABASE attr_db");
        }
        catch (const SQLite::Exception& e) {
            // remove broken database file and forward the exception
            if(std::filesystem::exists(sqlite_path))
                std::filesystem::remove(sqlite_path);
            throw std::runtime_error(std::string("SQLite error: ") + e.what());
        }

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
     *  In that case, either all three or none of the attribute_* parameters must be provided.
     *  If they are provided, the label attributes for the CSGV database are imported from the given
     *  attribute_table in the attribute_database and the attribute_label is used as the key column for voxel labels in the volume file.
     */
    void importOrProcessChunkedVolume(const std::string& volume_input_path, const std::string& sqlite_output_path,
                                      const std::string& attribute_database = "", const std::string& attribute_table = "", const std::string& attribute_label = "",
                                      bool chunked_input_data = false, glm::uvec3 max_file_index = glm::uvec3(0u)) {
        if(!std::filesystem::exists(sqlite_output_path)) {
            processVolumeAndCreateSqlite(sqlite_output_path, volume_input_path,
                                         attribute_database, attribute_table, attribute_label,
                                         chunked_input_data, max_file_index);
        }
        else {
            importFromSqlite(sqlite_output_path);
        }
    }

    void importFromSqlite(const std::string& sqlite_path) {
        m_db = std::make_unique<SQLite::Database>(sqlite_path, SQLite::OPEN_READONLY);
    }

    /** For a (possibly chunked) volume, the following preprocessing is carried out and exported to a new database:\n
     * 1. total number of voxels in the volume and the size of the (0,0,0) chunk\n
     * 2.
     */
    void processVolumeAndCreateSqlite(const std::string& sqlite_export_path, const std::string& volume_input_path,
                                      const std::string& attribute_database, const std::string& attribute_table,
                                      const std::string& label_column,
                                      bool chunked_input_data = false, glm::uvec3 max_file_index = glm::uvec3(0u)) {
        std::shared_ptr<Volume<uint32_t>> volume = nullptr;
        std::unordered_set<uint32_t> label_set = {};    // hash set to speed up the {label already exists} check
        std::vector<uint32_t> index_to_label = {};

        // iterate over all chunk files in morton order
        bool chunk_dimensions_vary = false;
        size_t chunk_index1D = 0ul;
        glm::uvec3 chunk_index(0ul);
        glm::uvec3 volume_dimension(0u);
        glm::uvec3 chunk_dimension(0u);
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
                        chunk_dimension[0] = cur_chunk_dim.x;
                        chunk_dimension[1] = cur_chunk_dim.y;
                        chunk_dimension[2] = cur_chunk_dim.z;
                    } else if(chunk_dimension[0] != cur_chunk_dim.x
                           || chunk_dimension[1] != cur_chunk_dim.y
                           || chunk_dimension[2] != cur_chunk_dim.z) {
                        chunk_dimensions_vary = true;
                    }

                    // update tracking information
                    volume_dimension[0] += cur_chunk_dim.x;
                    volume_dimension[1] += cur_chunk_dim.y;
                    volume_dimension[2] += cur_chunk_dim.z;

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
            Logger(WARN) << "chunk dimensions vary and can differ from expected dimension " << str(chunk_dimension);

        // create new SQLite database, export all data and then re-import as read only
        databaseExportAndOpen(sqlite_export_path, index_to_label, volume_dimension, chunk_dimension,
                              attribute_database, attribute_table, label_column);
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
        std::string label_column_name = m_db->execAndGet("SELECT label_column FROM " + INFO_TABLE).getString();
        uint32_t columns = m_db->execAndGet("SELECT COUNT(*) FROM " + ATTRIBUTE_TABLE).getUInt();
        auto label_to_index = std::make_shared<std::unordered_map<uint32_t, uint32_t>>();
        label_to_index->reserve(columns);

        // fill the map with entries for all labels
        SQLite::Statement query(*m_db, "SELECT " + ID_COLUMN + ", " + label_column_name + " FROM " + ATTRIBUTE_TABLE);
        const int id_column = query.getColumnIndex(ID_COLUMN.c_str());
        const int label_column = query.getColumnIndex(label_column_name.c_str());
        while (query.executeStep())
            (*label_to_index)[query.getColumn(label_column)] = query.getColumn(id_column);

        return label_to_index;
    }

private:
    std::unique_ptr<SQLite::Database> m_db = nullptr;   // sqlite database
};


} // namespace vvv

#endif // ifdef LIB_SQLITE3
