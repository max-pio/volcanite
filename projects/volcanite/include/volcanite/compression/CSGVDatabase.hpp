#pragma once

#include "vvv/volren/Volume.hpp"
#include "vvv/util/space_filling_curves.hpp"

#include "volcanite/compression/CompSegVolHandler.hpp"

#include <glm/glm.hpp>

#include <memory>
#include <string>
#include <utility>
#include <vector>

#ifdef LIB_SQLITE3
    #include <SQLiteCpp/SQLiteCpp.h>
    #include <SQLiteCpp/VariadicBind.h>
    #include "SQLiteCpp/VariadicBind.h"
#else
namespace SQLite {
    typedef char Database;
}
#endif

namespace vvv {

class CSGVDatabase {

private:
    const std::string CSGV_ATTRIBUTE_TABLE = "csgv_attribute";
    const std::string CSGV_INFO_TABLE = "csgv_info";
    const std::string ID_COLUMN = "csgv_id";

    /**
     * Exports preprocessing results to a new database after which it is opened in read mode.
     */
    bool databaseExportAndOpen(const std::string& sqlite_path, const std::vector<uint32_t>& index_to_label,
                               glm::uvec3 volume_dimension, glm::uvec3 chunk_dimension,
                               const std::string& attribute_database, std::string attribute_table,
                               std::string label_column) {
#ifdef LIB_SQLITE3
        if(m_db) {
            Logger(WARN) << "closing existing csgv database " << m_db->getFilename() << " before creation";
            close();
        }

        MiniTimer t;
        try{
            std::string csgv_label_name = label_column.empty() ? "label" : label_column;
            SQLite::Database db(sqlite_path, SQLite::OPEN_READWRITE | SQLite::OPEN_CREATE);
            // store general volume info
            {
                db.exec("CREATE TABLE " + CSGV_INFO_TABLE +
                        " (volume_width INTEGER, volume_height INTEGER, volume_depth INTEGER, chunk_width INTEGER, chunk_height INTEGER, chunk_depth INTEGER, label_column TEXT)");
                SQLite::Statement query(db, "INSERT INTO " + CSGV_INFO_TABLE + " VALUES (?, ?, ?, ?, ?, ?, ?)");
                SQLite::bind(query, volume_dimension[0], volume_dimension[1], volume_dimension[2],
                             chunk_dimension[0], chunk_dimension[1], chunk_dimension[2], csgv_label_name);
                if (query.exec() != 1)
                    Logger(WARN) << "Could not export " + CSGV_INFO_TABLE + " to sqlite";
            }

            // store mapping of original volume label <-> our packed csgv ids in a temporary table
            {
                db.exec("CREATE TABLE " + CSGV_ATTRIBUTE_TABLE + "_tmp (" + ID_COLUMN + " UNSIGNED INT PRIMARY KEY, " + csgv_label_name +
                        " UNSIGNED INT UNIQUE)");

                SQLite::Transaction transaction(db);
                SQLite::Statement query(db, "INSERT INTO " + CSGV_ATTRIBUTE_TABLE + "_tmp VALUES (?, ?)");
                for (uint32_t i = 0u; i < index_to_label.size(); i++) {
                    SQLite::bind(query, i, index_to_label[i]);
                    if (query.exec() != 1)
                        Logger(WARN) << "Could not insert entry for label into sqlite database";
                    query.reset();
                }
                transaction.commit();
            }

            Logger(DEBUG) << "  exported label remapping to database " << sqlite_path << " in " << t.restart() << " seconds";

            // There are no attributes so we just use the label remapping table (2 columns) without any additional data
            if(attribute_database.empty()) {
                db.exec("ALTER TABLE " + CSGV_ATTRIBUTE_TABLE + "_tmp RENAME TO " + CSGV_ATTRIBUTE_TABLE);
            }
            // Add attributes from existing database
            else {
                // ToDo: allow attribute_database to be a csv filepath and import from csv instead
                if(attribute_database.ends_with(".csv"))
                    throw std::runtime_error("Importing attributes from CSV files is not yet supported!");


                db.exec("ATTACH DATABASE '" + attribute_database + "' AS attr_db");

                // if not attribute table or label column was specified, we use the first table and its primary key
                if (attribute_table.empty() || label_column.empty()) {

                    if(attribute_table.empty()) {
                        attribute_table = db.execAndGet("SELECT name FROM attr_db.sqlite_master WHERE type='table'").getString();
                        if(attribute_table.empty())
                            throw std::runtime_error("Could not find any table to use in attribute database. Provide attribute table and label column name with the attribute database.");
                    }

                    SQLite::Statement pk_query(db, "SELECT l.name FROM pragma_table_info('" + attribute_table + "','attr_db') as l WHERE l.pk = 1");
                    if(!pk_query.executeStep()) {
                        throw std::runtime_error("Could not find any primary key in table '" + attribute_table + "'. Provide attribute table and label column name with the attribute database.");
                    }
                    label_column = pk_query.getColumn(0).getString();
                    Logger(DEBUG) << "  using attribute table '" << attribute_table << "' with primary key label column '" << label_column << "'";
                }

                // 0. check if the provide label column only contains unique elements
                {
                    SQLite::Statement check_duplicates(db,
                                                       "SELECT " + label_column + ", COUNT(*) AS cnt FROM attr_db." +
                                                       attribute_table + " GROUP BY " + label_column +
                                                       " HAVING cnt > 1");
                    if (check_duplicates.executeStep())
                        throw SQLite::Exception(
                                "Label column " + label_column + " in " + attribute_database + " : " + attribute_table +
                                " contains forbidden duplicate entries.");
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

                // 2. create the final attribute table and populate it with a join of the _tmp label remapping table and the provided attribute database
                {
                    // create an index on the label column in the databases if there are many labels
                    bool created_index_on_attached_db = false;
                    if(index_to_label.size() >= 65536) {
                        db.exec("CREATE UNIQUE INDEX csgv_label_index ON " + CSGV_ATTRIBUTE_TABLE + "_tmp(" +
                                csgv_label_name + ")");
                        try {
                            // attention! this creates an index in the ATTACHED database. We will drop it later.
                            db.exec("CREATE UNIQUE INDEX attr_db.csgv_label_index ON " + attribute_table + "(" +
                                    label_column + ")");
                            created_index_on_attached_db = true;
                        } catch (SQLite::Exception &e) {
                            Logger(WARN) << "  could not create index on attached database " << attribute_database;
                        }
                        Logger(DEBUG) << "  created indices on databases in " << t.restart() << " seconds";
                    }

                    std::stringstream create_table_ss;
                    create_table_ss << "CREATE TABLE " << CSGV_ATTRIBUTE_TABLE
                                    << " (" + ID_COLUMN + " UNSIGNED INT PRIMARY KEY, " + csgv_label_name +
                                       " UNSIGNED INT UNIQUE, ";
                    std::stringstream insert_ss;
                    insert_ss << "INSERT INTO " << CSGV_ATTRIBUTE_TABLE << " SELECT " << ID_COLUMN << ", "
                              << CSGV_ATTRIBUTE_TABLE << "_tmp." << csgv_label_name << " AS " << csgv_label_name
                              << ", ";
                    for (int i = 0; i < attr_col_names.size(); i++) {
                        create_table_ss << attr_col_names[i] << " " << attr_col_types[i] << ", ";
                        insert_ss << attr_col_names[i] << ", ";
                    }
                    // remove trailing ', '
                    std::string create_table_str = create_table_ss.str();
                    std::string insert_str = insert_ss.str();
                    if (attr_col_names.size() > 0) {
                        create_table_str.pop_back();
                        create_table_str.pop_back();
                        insert_str.pop_back();
                        insert_str.pop_back();
                    }
                    create_table_str.append(")");
                    insert_str.append(
                            " FROM " + CSGV_ATTRIBUTE_TABLE + "_tmp LEFT JOIN attr_db." + attribute_table + " ON " +
                            CSGV_ATTRIBUTE_TABLE + "_tmp." + csgv_label_name + " = " + "attr_db." + attribute_table +
                            "." + label_column);

                    SQLite::Transaction transaction(db);
                    db.exec(create_table_str);
                    db.exec(insert_str);
                    db.exec("DROP TABLE " + CSGV_ATTRIBUTE_TABLE + "_tmp");
                    transaction.commit();

                    // remove temporary index from attached database
                    if (created_index_on_attached_db) {
                        try {
                            db.exec("DROP INDEX IF EXISTS attr_db.csgv_label_index");
                        } catch (SQLite::Exception &e) {
                            Logger(WARN) << "Could not drop index csgv_label_index from attached database "
                                         << attribute_database;
                        }
                    }
                }

                Logger(DEBUG) << "  import attributes from existing database " << attribute_database << " in " << t.restart() << " seconds";
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
        importFromSqlite(sqlite_path);
        return true;
#else
        throw std::runtime_error("SQLite library not available");
#endif
    }

public:
    CSGVDatabase() = default;
    ~CSGVDatabase() { close(); }

    void close() {
        m_db = nullptr;
        m_attribute_names.clear();
        m_attribute_minmax.clear();
        m_label_count = 0;
    }

    /** This database will not contain any real information but will return a label count of uint32_MAX and a single
     * attribute name "csgv_id". This way it can be used in the csgv renderer which will implicitly map this single
     * attribute to the voxel labels from the csgv volume.
     */
    void createDummy(const CompressedSegmentationVolume* csgv) {
        m_db = nullptr;
        //ToDo: could create a in-memory database if we need more dummy functionality (if LIB_SQLITE3 is present)
        // m_db =  std::make_unique<SQLite::Database>(":memory:", SQLite::OPEN_MEMORY);
        m_label_count = ~0u;    // uint32 Max ToDo: find the maximum palette label within the csgv file
        m_attribute_names = {"csgv_id"};
        m_attribute_minmax = {glm::vec2(0.f, static_cast<float>(m_label_count))};
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
#ifdef LIB_SQLITE3
        m_db = std::make_unique<SQLite::Database>(sqlite_path, SQLite::OPEN_READONLY);

        // read label count, attribute names, and min/max values from columns
        m_label_count = m_db->execAndGet("SELECT COUNT(*) FROM " + CSGV_ATTRIBUTE_TABLE).getInt64();
        m_attribute_names.clear();
        m_attribute_minmax.clear();
        SQLite::Statement column_query(*m_db, "SELECT name FROM pragma_table_info('" + CSGV_ATTRIBUTE_TABLE + "') ORDER BY cid");
        while (column_query.executeStep()) {
            m_attribute_names.push_back(column_query.getColumn(0).getString());
            m_attribute_minmax.emplace_back(static_cast<float>(m_db->execAndGet("SELECT MIN(" + m_attribute_names.back() + ") FROM " + CSGV_ATTRIBUTE_TABLE).getDouble()),
                                            static_cast<float>(m_db->execAndGet("SELECT MAX(" + m_attribute_names.back() + ") FROM " + CSGV_ATTRIBUTE_TABLE).getDouble()));
        }
#else
        throw std::runtime_error("SQLite library not available");
#endif
    }

    /** For a (possibly chunked) volume, the following preprocessing is carried out and exported to a new database:\n
     * 1. total number of voxels in the volume and the size of the (0,0,0) chunk\n
     * 2.
     */
    void processVolumeAndCreateSqlite(const std::string& sqlite_export_path, const std::string& volume_input_path,
                                      const std::string& attribute_database, const std::string& attribute_table,
                                      const std::string& label_column,
                                      bool chunked_input_data = false, glm::uvec3 max_file_index = glm::uvec3(0u)) {
#ifdef LIB_SQLITE3
        std::shared_ptr<Volume<uint32_t>> volume = nullptr;
        std::unordered_set<uint32_t> label_set = {};    // hash set to speed up the {label already exists} check
        std::vector<uint32_t> index_to_label = {};

        MiniTimer t;
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
                    Logger(DEBUG, true) << "  label preprocessing " << chunk_input_path << " "
                    << (1 + sfc::Cartesian::p2i(chunk_index, max_file_index + glm::uvec3(1))) << "/" << (1 + sfc::Cartesian::p2i(max_file_index, max_file_index + glm::uvec3(1)));

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
#if 1
                    {
                        const uint64_t last_i = sfc::Morton3D::p2i_64(cur_chunk_dim);
                        const int NUM_THREADS = 8;
                        size_t voxels_per_thread = (32ul * 32ul * 32ul);
                        // parallel processing will only have a benefit if we can run at least 4 threads in parallel
                        if(last_i < 4ul * voxels_per_thread) {
                            voxels_per_thread = last_i;
                            //NUM_THREADS = 1;
                        }

                        // iterate over all voxels in the volume to create a list of unique labels
                        // we first store the labels in a thread-private _index_to_label and gather them later in the global map
                        std::vector<uint32_t> _index_to_label[NUM_THREADS];
                        std::unordered_set<uint32_t> _label_set[NUM_THREADS];

                        for(auto& v: _index_to_label)
                            v.reserve(voxels_per_thread);
                        for(size_t i = 0; i < last_i; i += (NUM_THREADS * voxels_per_thread)) {
                            // process the next NUM_THREADS * voxels_per_thread count voxels in parallel
                            #pragma omp parallel num_threads(NUM_THREADS) default(none) shared(i, voxels_per_thread, cur_chunk_dim, volume, label_set, _index_to_label, _label_set)
                            {
                                unsigned int thread_id = omp_get_thread_num();
                                _index_to_label[thread_id].clear();
                                _label_set[thread_id].clear();

                                for(uint64_t n = i + thread_id * voxels_per_thread; n < i + (thread_id + 1) * voxels_per_thread; n++) {
                                    glm::uvec3 voxel = sfc::Morton3D::i2p_64(n);
                                    if (glm::all(glm::lessThan(voxel, cur_chunk_dim))) {
                                        uint32_t label = volume->getElement(voxel);
                                        if (!_label_set[thread_id].contains(label) && !label_set.contains(label)) {
                                            _index_to_label[thread_id].push_back(label);
                                            // we can not add the label to the global label set here, because it may create duplicate entries
                                            _label_set[thread_id].insert(label);
                                        }
                                    }
                                }
                            }

                            // gather all thread-private label sets into the global map
                            for(int thread_id = 0; thread_id < NUM_THREADS; thread_id++) {
                                for(const auto& label : _index_to_label[thread_id]) {
                                    if (!label_set.contains(label)) {
                                        index_to_label.push_back(label);
                                        label_set.insert(label);
                                    }
                                }
                            }


                            if(i % (last_i / 100) == 0u)
                                Logger(INFO, true) << " re-labelling map computation " << static_cast<int>(static_cast<float>(i)/static_cast<float>(last_i) * 100.f) << "%";
                        }

                        if(index_to_label.size() != label_set.size())
                            throw std::runtime_error("existing_labels set does not match index_to_label size in volume label occurrence processing");
                    }
#else
                {
                    // ToDo: parallelize with OpenMP?
                    const uint64_t last_i = sfc::Morton3D::p2i_64(cur_chunk_dim);
                    for(uint64_t i = 0; i < last_i; i++) {
                        glm::uvec3 voxel = sfc::Morton3D::i2p_64(i);
                        if(glm::all(glm::lessThan(voxel, cur_chunk_dim))) {
                            uint32_t label = volume->getElement(voxel);
                            if (!label_set.contains(label)) {
                                index_to_label.push_back(label);
                                label_set.insert(label);
                            }
                        }

                        if(i % (last_i / 100) == 0u)
                            Logger(INFO, true) << " re-labelling map computation " << static_cast<int>(static_cast<float>(i)/static_cast<float>(last_i) * 100.f) << "%";
                    }

                    if(index_to_label.size() != label_set.size())
                        throw std::runtime_error("existing_labels set does not match index_to_label size in volume label occurrence processing");
                }
#endif
            }

            chunk_index1D++;
        } while(chunked_input_data && glm::any(glm::lessThanEqual(chunk_index, max_file_index)));

        if(chunk_dimensions_vary)
            Logger(WARN) << "  chunk dimensions vary and can differ from expected dimension " << str(chunk_dimension);

        Logger(DEBUG) << "  computed label remapping in " << t.elapsed() << " seconds with " << index_to_label.size() << " unique labels";

        // create new SQLite database, export all data and then re-import as read only
        databaseExportAndOpen(sqlite_export_path, index_to_label, volume_dimension, chunk_dimension,
                              attribute_database, attribute_table, label_column);
#else
        throw std::runtime_error("SQLite library not available");
#endif
    }

    /**
     * Returns a mapping of the original volume's labels to new voxel ids that are\n
     * (1) one continuous space, i.e. [0, N) for N unique labels in the volume\n
     * (2) ordered along a Morton Z-Curve by their first appearance in the volume
     */
    [[nodiscard]] std::shared_ptr<std::unordered_map<uint32_t, uint32_t>> getLabelRemapping() const {
#ifdef LIB_SQLITE3
        if(!m_db)
            throw std::runtime_error("No CSGV sqlite database present.");

        // clear the label_to_index map and reserve memory
        std::string label_column_name = m_db->execAndGet("SELECT label_column FROM " + CSGV_INFO_TABLE).getString();
        uint32_t columns = m_db->execAndGet("SELECT COUNT(*) FROM " + CSGV_ATTRIBUTE_TABLE).getUInt();
        auto label_to_index = std::make_shared<std::unordered_map<uint32_t, uint32_t>>();
        label_to_index->reserve(columns);

        // fill the map with entries for all labels
        SQLite::Statement query(*m_db, "SELECT " + ID_COLUMN + ", " + label_column_name + " FROM " + CSGV_ATTRIBUTE_TABLE);
        const int id_column = query.getColumnIndex(ID_COLUMN.c_str());
        const int label_column = query.getColumnIndex(label_column_name.c_str());
        while (query.executeStep())
            (*label_to_index)[query.getColumn(label_column)] = query.getColumn(id_column);

        return label_to_index;
#else
        throw std::runtime_error("SQLite library not available");
#endif
    }

    size_t getAttributeCount() const {
        return m_attribute_names.size();
    }

    const std::vector<std::string>& getAttributeNames() const {
        return m_attribute_names;
    }

    const std::vector<glm::vec2>& getAttributeMinMax() const {
        return m_attribute_minmax;
    }

    size_t getLabelCount() const {
        return m_label_count;
    }

    /**
     * Fills the memory area with the float attribute for the given attribute index. The buffer must be large enough
     * to fit getLabelCount() elements. If maxSize > getLabelCount(), only getLabelCount() elements are written.
     *
     * @return the number of written elements
     */
    size_t getAttribute(int attributeIndex, float* begin, size_t maxSize) {
#ifdef LIB_SQLITE3
        if(!m_db)
            throw std::runtime_error("No CSGV sqlite database present.");

        if(attributeIndex >= m_attribute_names.size())
            throw std::runtime_error("invalid attribute index " + std::to_string(attributeIndex));
        if(maxSize < m_label_count)
            throw std::runtime_error("Buffer for attribute " + m_attribute_names[attributeIndex] + "(" + std::to_string(attributeIndex) + ") does not fit " + std::to_string(m_label_count) + " elements.");

        SQLite::Statement query(*m_db, "SELECT " + m_attribute_names[attributeIndex] + " FROM " + CSGV_ATTRIBUTE_TABLE);
        float* it = begin;
        while (query.executeStep()) {
            *it = static_cast<float>(query.getColumn(0).getDouble());
            it++;
        }
        assert((it == begin + m_label_count) && "Did not write expected number of attribute values");
        return (it - begin);
#else
        throw std::runtime_error("SQLite library not available");
#endif
    }

private:
    std::unique_ptr<SQLite::Database> m_db = nullptr;   // sqlite database
    std::vector<std::string> m_attribute_names = {};
    std::vector<glm::vec2> m_attribute_minmax = {};
    size_t m_label_count = 0;
};


} // namespace vvv
