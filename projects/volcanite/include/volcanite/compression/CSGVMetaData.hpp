#pragma once

#include "vvv/volren/Volume.hpp"
#include "vvv/util/space_filling_curves.hpp"

#include "volcanite/compression/CompSegVolHandler.hpp"

#include <glm/glm.hpp>

#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace vvv {

class CSGVMetaData {
private:
    const std::string CURRENT_MCSV_HEADER = "MCSV0";
public:
    CSGVMetaData() = default;

    bool importFromFile(const std::string& msgv_file) {
        std::ifstream fin(msgv_file, std::ios::in);
        if(!fin.is_open()) {
            Logger(WARN) << "could not open file " << msgv_file << " to import volume meta data";
            return false;
        }

        std::string header;
        getline(fin, header);
        if(header != CURRENT_MCSV_HEADER) {
            Logger(WARN) << "Header " << header << " in file " << msgv_file << " does not match expected " << CURRENT_MCSV_HEADER;
            fin.close();
            return false;
        }

        fin >> m_volume_dimension[0] >> m_volume_dimension[1] >> m_volume_dimension[2];
        fin >> m_chunk_dimension[0] >> m_chunk_dimension[1] >> m_chunk_dimension[2];
        size_t label_count;
        fin >> label_count;
        m_index_to_label.resize(label_count);
        for(size_t i = 0ul; i < label_count; i++)
            fin >> m_index_to_label[i];

        fin.close();
        return true;
    }

    bool exportToFile(const std::string& msgv_file) {

        // ToDo: storing this information uncompressed in plaintext may be a huge memory problem! for 64,000,000 labels, we would approximately store almost a GB

        std::ofstream fout(msgv_file, std::ios::out);
        if(!fout.is_open()) {
            Logger(WARN) << "could not open file " << msgv_file << " to export volume meta data";
            return false;
        }

        fout << CURRENT_MCSV_HEADER << std::endl;
        fout << m_volume_dimension[0] << " " << m_volume_dimension[1] << " " << m_volume_dimension[2] << std::endl;
        fout << m_chunk_dimension[0] << " " << m_chunk_dimension[1] << " " << m_chunk_dimension[2] << std::endl;
        fout << m_index_to_label.size() << std::endl;
        for(const auto& l : m_index_to_label)
            fout << l << std::endl;

        fout.close();
        return true;
    }

    /** Iterates over all voxels in the given volume in morton order and pushes any labels that are not yet included
     * int index_to_label to index_to_label. Can be used to iteratively process multiple chunks of a single volume.
     * If the set existing_labels is provided to speed up the search, it must contain all labels from index_to_label.
     */
    void addLabelsFromVolume(std::shared_ptr<Volume<uint32_t>> volume, std::vector<uint32_t>& index_to_label, std::set<uint32_t>* existing_labels = nullptr) {

        std::set<uint32_t>* label_set = existing_labels ? existing_labels : new std::set<uint32_t>(index_to_label.begin(), index_to_label.end());

        if(index_to_label.size() != label_set->size())
            throw std::runtime_error("existing_labels set does not match index_to_label size in volume label occurrence processing");

        // iterate over all voluem voxels in morton order
        size_t i = 0ul;
        glm::uvec3 voxel(0ul);
        glm::uvec3 vol_dim(volume->dim_x, volume->dim_y, volume->dim_z);

        // ToDo: parallelize with OpenMP
        do {
            if(glm::all(glm::lessThan(voxel, vol_dim))) {
                uint32_t label = volume->getElement(voxel);
                if (!label_set->contains(label)) {
                    label_set->insert(label);
                    index_to_label.push_back(label);
                }
            }

            i++;
            voxel = sfc::Morton3D::i2p(i);
        } while(glm::any(glm::lessThan(voxel, vol_dim)));

        if(!existing_labels)
            delete label_set;
    }

    void processVolume(const std::string& input_path, bool chunked_input_data = false, glm::uvec3 max_file_index = glm::uvec3(0u)) {

        std::shared_ptr<Volume<uint32_t>> volume = nullptr;
        m_index_to_label.clear();
        m_existing_labels.clear();

        // iterate over all chunk files in morton order
        size_t chunk_index1 = 0ul;
        glm::uvec3 chunk_index(0ul);
        do {

            if(glm::all(glm::lessThanEqual(chunk_index, max_file_index))) {
                    // create file input and output paths for this single chunk
                    std::string chunk_input_path = chunked_input_data ? CompSegVolHandler::formatChunkPath(input_path, chunk_index.x, chunk_index.y, chunk_index.z)
                                                                      : input_path;
                    // load chunk volume
                    CompSegVolHandler::loadSegmentationVolumeFile(chunk_input_path, volume);
                    glm::uvec3 cur_chunk_dim(volume->dim_x, volume->dim_y, volume->dim_z);

                    // process chunk volume
                    if(chunk_index1 == 0ul) {
                        m_chunk_dimension[0] = cur_chunk_dim.x;
                        m_chunk_dimension[1] = cur_chunk_dim.y;
                        m_chunk_dimension[2] = cur_chunk_dim.z;
                    } else if(m_chunk_dimension[0] != cur_chunk_dim.x
                           || m_chunk_dimension[1] != cur_chunk_dim.y
                           || m_chunk_dimension[2] != cur_chunk_dim.z) {
                        Logger(WARN) << "chunk " << chunk_input_path << " dimension " << str(cur_chunk_dim)
                        << " differs from expected dimension " << str(glm::uvec3(m_chunk_dimension[0], m_chunk_dimension[1], m_chunk_dimension[2]));
                    }

                    // update tracking information
                    m_volume_dimension[0] += cur_chunk_dim.x;
                    m_volume_dimension[1] += cur_chunk_dim.y;
                    m_volume_dimension[2] += cur_chunk_dim.z;
                    addLabelsFromVolume(volume, m_index_to_label, &m_existing_labels);
            }

            chunk_index1++;
            chunk_index = sfc::Morton3D::i2p(chunk_index1);
        } while(chunked_input_data && glm::any(glm::lessThanEqual(chunk_index, max_file_index)));

        // output meta data information to file
        std::string output_path = chunked_input_data ? CompSegVolHandler::combinedPath(input_path, max_file_index) : input_path;
        exportToFile(output_path + ".msgv");
    }

    /** Tries to load a precomputed .msgv file for the given volume.
     *     If it does not exists, the .msgv file is created after preprocessing the volume instead.
     */
    void importOrProcessVolume(const std::string& input_path, bool chunked_input_data = false, glm::uvec3 max_file_index = glm::uvec3(0u)) {
        std::string msgv_path = chunked_input_data ? CompSegVolHandler::combinedPath(input_path, max_file_index) : input_path;
        msgv_path += ".msgv";
        if(!std::filesystem::exists(msgv_path) || !importFromFile(msgv_path)) {
            processVolume(input_path, chunked_input_data, max_file_index);
        }
    }

private:
    std::set<uint32_t> m_existing_labels = {};                 // hash set to speed up the {label already exists} check
    std::vector<uint32_t> m_index_to_label = {};               // list of unique labels in Morton order of occurrence
    uint32_t m_volume_dimension[3] = {0u, 0u, 0u};             // total size in voxels
    uint32_t m_chunk_dimension[3] = {0u, 0u, 0u};              // dimension of a single chunk
};


} // namespace vvv
