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

#include "volcanite/compression/CompressedSegmentationVolume.hpp"

#include "volcanite/compression/encoder/NibbleEncoder.hpp"
#include "volcanite/compression/encoder/RangeANSEncoder.hpp"
#include "volcanite/compression/encoder/WaveletMatrixEncoder.hpp"

#include <thread>

using namespace vvv;

namespace volcanite {

void CompressedSegmentationVolume::setCompressionOptions(uint32_t brick_size, EncodingMode encoding_mode,
                                                         uint32_t op_mask, bool random_access,
                                                         const uint32_t *code_frequencies,
                                                         const uint32_t *detail_code_frequencies) {
    if (!(brick_size > 0 && !(brick_size & (brick_size - 1))))
        throw std::runtime_error("Brick size must be a power of two greater than zero.");
    if (!m_encodings.empty()) {
        Logger(Warn) << "CompressedSegmentationVolume was already compressed. Clearing old data on new config.";
        clear();
    }

    m_brick_size = brick_size;
    m_encoding_mode = encoding_mode;
    m_op_mask = op_mask;
    m_random_access = random_access;

    // TODO: replace with switch / case
    // set up the respective brick encoder
    if (m_encoding_mode == NIBBLE_ENC) {
        if (m_random_access && (m_op_mask & OP_PALETTE_D_BIT))
            throw std::runtime_error("Nibble random access encoding does not support PALETTE_DELTA operation.");
        if (m_random_access && (m_op_mask & OP_STOP_BIT))
            throw std::runtime_error("Nibble random access encoding does not support stop bits.");
        m_encoder = std::make_unique<NibbleEncoder>(m_brick_size, m_encoding_mode, m_op_mask);
    } else if (m_encoding_mode == SINGLE_TABLE_RANS_ENC || m_encoding_mode == DOUBLE_TABLE_RANS_ENC) {
        if (code_frequencies == nullptr)
            throw std::runtime_error("Operation frequencies must be given if using rANS.");
        if (random_access)
            throw std::runtime_error("Random access encoding is not compatible with rANS.");

        // normalize the symbol frequencies and setup encoder
        m_encoder = std::make_unique<RangeANSEncoder>(m_brick_size, m_encoding_mode, m_op_mask,
                                                      normalizeCodeFrequencies(code_frequencies).data(),
                                                      (m_encoding_mode == DOUBLE_TABLE_RANS_ENC)
                                                          ? normalizeCodeFrequencies(detail_code_frequencies).data()
                                                          : nullptr);
    } else if (m_encoding_mode == WAVELET_MATRIX_ENC || m_encoding_mode == HUFFMAN_WM_ENC) {
        if (m_random_access && (m_op_mask & OP_PALETTE_D_BIT))
            throw std::runtime_error("Wavelet Matrix encoding does not support PALETTE_DELTA operation.");
        if (m_encoding_mode == WAVELET_MATRIX_ENC && (m_op_mask & OP_STOP_BIT))
            throw std::runtime_error("Wavelet Matrix encoding (without Huffman) does not support stop bits.");
        m_encoder = std::make_unique<WaveletMatrixEncoder>(m_brick_size, m_encoding_mode, m_op_mask);
    } else {
        throw std::runtime_error("No CSGV brick encoder for given encoding mode available.");
    }
    m_encoder->setCPUThreadCount(m_cpu_threads);
    m_encoder->setDecodeWithSeparateDetail(m_separate_detail);
}

float CompressedSegmentationVolume::separateDetail() {
    if (m_random_access)
        throw std::runtime_error("Detail separation and random access cannot be combined.");

    if (!m_detail_encodings.empty() || m_separate_detail)
        throw std::runtime_error("Detail separation was already performed!");
    if (m_encodings.empty())
        throw std::runtime_error("Segmentation volume is not yet compressed! Call compress() before performing detail separation.");
    if (m_encoding_mode != DOUBLE_TABLE_RANS_ENC)
        throw std::runtime_error("Detail separation can only be used in combination with rANS in double table mode!");

    const uint32_t brick_idx_count = getBrickIndexCount();

    // First, construct the detail_starts buffer in a simple sequential pass and keep track of detail encoding sizes:
    std::vector<uint32_t> split_detail_encoding_sizes(1, 0u);
    uint32_t currentDetailStart = 0u;
    m_detail_starts.resize(brick_idx_count + 1);
    for (size_t i = 0; i < brick_idx_count; i++) {
        // Write the current "brick start" before the possible splitting of encodings as it is the "previous brick end"
        m_detail_starts[i] = currentDetailStart;

        // if a new split encoding starts, restart index counter and keep track of the previous detail array size
        if (i / m_brick_idx_to_enc_vector >= split_detail_encoding_sizes.size()) {
            split_detail_encoding_sizes.back() = currentDetailStart;
            split_detail_encoding_sizes.push_back(0u);
            currentDetailStart = 0u;
        }

        // the encoder specifies how many uint32_t elements are required to store this brick's detail encoding
        currentDetailStart += m_encoder->getDetailLengthBeforeSeparation(getBrickEncoding(i), getBrickEncodingLength(i));

        // TODO: just separate the detail encoding here, let it return the number of uint32s needed? handle split encodings.
    }
    split_detail_encoding_sizes.back() = currentDetailStart;
    m_detail_starts[brick_idx_count] = currentDetailStart;

    // Second, cut the operation encoding arrays apart und update brick headers / base encoding starts.
    // The same brick_idx to split (detail) encoding vector is used for base and detail encodings.
    // Handle one brick after another, splitting encoding arrays if necessary:
    m_detail_encodings.resize(1);
    m_detail_encodings.back().resize(split_detail_encoding_sizes.at(0));

    // the first brick always starts at the first entry
    m_brick_starts[0] = 0u;
    // keeping track of the start and end of the next brick is required, as brick ends (= next brick's start) contents
    // are overwritten on the go.
    uint32_t next_old_brick_start = getBrickStart(0); // is zero
    uint32_t next_old_brick_length = getBrickEncodingLength(0);
    // note: it is possible to process all split encoding arrays in parallel, but this would increase memory load
    uint32_t cur_base_enc_brick_end = 0u;
    for (uint32_t brick_idx = 0u; brick_idx < brick_idx_count; brick_idx++) {

        uint32_t detail_start = m_detail_starts[brick_idx];
        // if this is the first brick in a split encoding array:
        if (brick_idx / m_brick_idx_to_enc_vector >= m_detail_encodings.size()) {
            // start a new detail encoding array
            m_detail_encodings.emplace_back(split_detail_encoding_sizes.at(brick_idx / m_brick_idx_to_enc_vector));
            detail_start = 0u;
            // finish the last (now completed) base encoding vector and shrink to fit
            m_encodings.at((brick_idx - 1u) / m_brick_idx_to_enc_vector).resize(m_brick_starts[brick_idx]);
            cur_base_enc_brick_end = 0u;
            assert(brick_idx % m_brick_idx_to_enc_vector == 0 && "new split encoding does not start with first brick");
            assert(next_old_brick_start == 0u && "base encoding and new detail encoding start at different split points");
        }
        uint32_t detail_encoding_size = m_detail_starts[brick_idx + 1] - detail_start;

        // operate directly on the current brick base encoding array
        auto &mut_encoding = m_encodings[brick_idx / m_brick_idx_to_enc_vector];

        // determine the new output position of this brick in the base encoding output array (overwriting old content)
        // TODO: the first brick in a split encoding must start at 0 instead of currentBaseEncBrickEnd, but this has to be correct to mark the end
        uint32_t op_base_encoding_length = m_encoder->separateDetail({mut_encoding.begin() + next_old_brick_start, next_old_brick_length},
                                                                     {mut_encoding.data() + cur_base_enc_brick_end, mut_encoding.size() - cur_base_enc_brick_end},
                                                                     {&(m_detail_encodings.at(brick_idx / m_brick_idx_to_enc_vector).at(detail_start)),
                                                                      m_detail_encodings.at(brick_idx / m_brick_idx_to_enc_vector).size() - detail_start});
        assert(op_base_encoding_length < next_old_brick_length && "new base encoding size larger than old brick encoding after separateDetail");

        cur_base_enc_brick_end += op_base_encoding_length;
        // read the next brick information before updating the brick end (= overwrite the next brick's start)
        if (brick_idx < brick_idx_count - 1u) {
            next_old_brick_start = getBrickStart(brick_idx + 1);
            next_old_brick_length = getBrickEncodingLength(brick_idx + 1);
        }
        m_brick_starts[brick_idx + 1] = cur_base_enc_brick_end;
    }
    // shrink last encoding buffer
    m_encodings.back().resize(m_brick_starts[brick_idx_count]);

    m_separate_detail = true;
    m_encoder->setDecodeWithSeparateDetail(true);

    if (!verifyCompression()) {
        throw std::runtime_error("Corrupt CSGV after detail separation");
    }

    // return the ratio of detail encoding size to total encoding size
    return (static_cast<float>(m_detail_starts[brick_idx_count]) / static_cast<float>(m_brick_starts[brick_idx_count] + m_detail_starts[brick_idx_count]));
}

bool CompressedSegmentationVolume::verifyCompression() const {
    if (m_encodings.empty())
        throw std::runtime_error("Segmentation volume is not yet compressed!");

    if (static_cast<size_t>(m_volume_dim.x) * m_volume_dim.y * m_volume_dim.z == 0ull) {
        Logger(Error) << "  volume size is zero with voxel dimension " << str(m_volume_dim);
        return false;
    }

    bool is_ok = true;
    glm::uvec3 brick_count = getBrickCount();
    size_t last_brick = getBrickIndexCount() - 1ul;

    // check that all encodings have the size that is tracked in the brick starts arrays
    for (int i = 0; i < m_encodings.size(); i++) {
        // any m_brick_idx_to_enc_vector-th entry in brick_starts is the end of the last brick in the previous array
        uint32_t size_from_brick_starts = m_brick_starts[std::min(static_cast<uint32_t>(last_brick + 1), (i + 1) * m_brick_idx_to_enc_vector)];
        if (m_encodings.at(i).size() != size_from_brick_starts) {
            Logger(Error) << "  split encoding array [" << i << "/" << (m_encodings.size() - 1)
                          << "] size differs from size tracked in brick starts (is "
                          << m_encodings.at(i).size() << " expected " << size_from_brick_starts << ").";
            return false;
        }
    }

#pragma omp parallel for collapse(3) default(none) shared(is_ok, brick_count, m_brick_starts, m_encodings, m_detail_starts, m_detail_encodings)
    for (uint32_t z = 0u; z < brick_count.z; z++) {
        for (uint32_t y = 0u; y < brick_count.y; y++) {
            for (uint32_t x = 0u; x < brick_count.x; x++) {
                if (!is_ok)
                    continue;

                glm::uvec3 brick(x, y, z);
                uint32_t brick1D = brick_pos2idx(brick, brick_count);

                std::stringstream error = {};
                m_encoder->verifyBrickCompression(getBrickEncoding(brick1D), getBrickEncodingLength(brick1D),
                                                  isUsingSeparateDetail() ? getBrickDetailEncoding(brick1D) : nullptr,
                                                  isUsingSeparateDetail() ? getBrickDetailEncodingLength(brick1D) : 0u,
                                                  error);

                // check for 32 Bit overflow if bytes are indexed in the buffers
                {
                    if (brick1D > 0 && m_brick_starts[brick1D + 1u] == 0) {
                        error << "  brick start index array contains invalid zero after first entry";
                    }
                    if (static_cast<size_t>(m_brick_starts[brick1D + 1u]) > (~0u)) {
                        error << "  encoding contains more 32 bit entries ("
                              << (static_cast<size_t>(m_brick_starts[brick1D + 1u]))
                              << ") than 32 bit indices can index (" << (~0u) << ")\n";
                    }

                    if (isUsingSeparateDetail()) {
                        if (m_detail_starts[brick1D + 1u] == 0) {
                            error << "  brick detail start index array contains invalid zero after first entry";
                        }
                        if (static_cast<size_t>(m_detail_starts[brick1D + 1u]) > (~0u)) {
                            error << "  detail encoding contains more 32 bit entries ("
                                  << (static_cast<size_t>(m_detail_starts[brick1D + 1u]))
                                  << ") than 32 bit indices can index (" << (~0u) << ")\n";
                        }
                    }
                }

                // print error message
                if (!error.str().empty()) {
#pragma omp critical
                    {
                        if (is_ok) {
                            Logger(Error) << "Found errors for brick " << str(brick) << " #"
                                          << brick_pos2idx(brick, getBrickCount()) << ":\n"
                                          << error.str() << "---";
                            // TODO: loglevel ERROR does not work on windows. workaround outputs to INFO in that case
                            printBrickInfo(brick, vvv::loglevel(Error));
                            is_ok = false;
                        }
                    }
                }
            }
        }
    }
    return is_ok;
}

void CompressedSegmentationVolume::compress(const std::vector<uint32_t> &volume, const glm::uvec3 volume_dim, bool verbose) {
    if (m_brick_size == 0u)
        throw std::runtime_error("Compression parameters are not initialized!");

    m_volume_dim = volume_dim;
    glm::uvec3 brickCount = getBrickCount();
    if (verbose) {
        Logger(Debug) << " running with " << m_cpu_threads << " threads on " << std::thread::hardware_concurrency() << " CPU cores";
        Logger(Debug) << " brick count: " << str(brickCount) << " = " << getBrickIndexCount() << " with brick size " << m_brick_size << "^3";
    }

    // m_encodings contains > 0 vectors storing the brick encoding. For any brick with 1D index i, the corresponding
    // encoding vector index in m_encodings is obtained through (i / m_brick_idx_to_enc_vector).
    // m_brick_idx_to_enc_vector is set to UINT32_MAX initially and reduced during the compression aiming to store
    // m_enc_vector_limit many uint32_t entries in the first encoding vector.
    m_encodings.clear();
    size_t reserved_size = std::min(static_cast<size_t>(m_target_uints_per_split_encoding), static_cast<size_t>(volume_dim.x) * volume_dim.y * volume_dim.z / 12ul / 4ul); // assume that we have a compression rate below 1/12
    if (reserved_size > UINT32_MAX) {
        Logger(Warn) << "Volume is large, potentially creating a Compressed Segmentation Volume that does not fit into 32bit address!";
        reserved_size = UINT32_MAX;
    }
    // Start with one encoding vector. Once it is filled up to the target size m_enc_vector_limit,
    // m_brick_idx_to_enc_vector is updated to start a new encoding vector for the next brick index.
    m_encodings.emplace_back();
    m_encodings[0].reserve(reserved_size);
    uint32_t brick_index_count = getBrickIndexCount();
    m_brick_starts.resize(brick_index_count + 1, INVALID);
    // reset brick to split encoding vector mapping, and max. palette entry count
    m_brick_idx_to_enc_vector = ~0u;
    m_max_brick_palette_count = 0u;

    // detail buffers can only be filled with a subsequent call to separateDetail()
    m_separate_detail = false;
    m_detail_encodings.clear();
    m_detail_starts.clear();

    if (verbose)
        Logger(Info, true) << getLabel() << " Compression Progress 0.0%";
    MiniTimer progressTimer;
    MiniTimer totalTimer;
    uint32_t bricks_since_last_update = 0;

    // compute the next m_cpu_threads brick encodings in parallel
    // we assume that the worst case compression rate is 100% and allocate encoding buffers accordingly
    const uint32_t encoded_brick_buffer_size = m_brick_size * m_brick_size * m_brick_size;
    std::vector<uint32_t> encodedBrick[m_cpu_threads];
    uint32_t encoded_element_count[m_cpu_threads];
    uint32_t encoded_element_count_prefix_sum[m_cpu_threads];
    for (int thread_id = 0; thread_id < m_cpu_threads; thread_id++) {
        encodedBrick[thread_id].resize(encoded_brick_buffer_size);
        encoded_element_count[thread_id] = 0u;
        encoded_element_count_prefix_sum[thread_id] = 0u;
    }

    // compress one brick after another (but m_cpu_threads of them in parallel) in brick_index order
    for (uint32_t brick_index = 0u; brick_index < brick_index_count; brick_index += m_cpu_threads) {

#pragma omp parallel num_threads(m_cpu_threads) default(none) shared(brick_index, brickCount, brick_index_count, volume, encodedBrick, encoded_element_count)
        {
            unsigned int thread_id = omp_get_thread_num();
            encoded_element_count[thread_id] = 0u;
            if (brick_index + thread_id < brick_index_count) {
                glm::uvec3 brick = brick_idx2pos(brick_index + thread_id, brickCount);
                // compress the current brick
                if (m_random_access)
                    encoded_element_count[thread_id] = m_encoder->encodeBrickForRandomAccess(volume, encodedBrick[thread_id], brick * m_brick_size, m_volume_dim);
                else
                    encoded_element_count[thread_id] = m_encoder->encodeBrick(volume, encodedBrick[thread_id], brick * m_brick_size, m_volume_dim);

                assert(encoded_element_count[thread_id] < encodedBrick[thread_id].size() && "Buffer overflow for encoded brick.");
            }
        }

        // an exclusive prefix sum of the element counts tells us the local offsets in the encoding buffer.
        // encoded_element_count_prefix_sum[0] is always 0. We also count how many new elements we need in total.
        for (int thread_id = 1; thread_id < m_cpu_threads; thread_id++) {
            encoded_element_count_prefix_sum[thread_id] = encoded_element_count_prefix_sum[thread_id - 1] + encoded_element_count[thread_id - 1];
        }
        size_t old_encoding_size = m_encodings.back().size();
        size_t new_encoding_size = old_encoding_size + encoded_element_count_prefix_sum[m_cpu_threads - 1] + encoded_element_count[m_cpu_threads - 1];

        // Check if we have to start a new encoding vector here. As m_brick_idx_to_enc_vector is always a multiple of
        // m_cpu_threads, either all or none of the new bricks belong to a new split encoding array.
        if (std::min(brick_index_count, (brick_index + m_cpu_threads - 1u)) / m_brick_idx_to_enc_vector >= m_encodings.size()) {
            m_encodings.back().shrink_to_fit();
            m_encodings.emplace_back();
            m_encodings.back().reserve(reserved_size);
            old_encoding_size = 0ul;
            new_encoding_size = encoded_element_count_prefix_sum[m_cpu_threads - 1] + encoded_element_count[m_cpu_threads - 1];
        }
        // Check if the initial split must happen here (when the uint32_t element count exceeds m_target_uints_per_split_encoding)
        else if (new_encoding_size > m_target_uints_per_split_encoding) {
            if (brick_index == 0u) {
                Logger(Warn) << "Requested split encoding size is too small. Using minimal size.";
            }
            // We can not reduce m_brick_idx_to_enc_vector further if it was already used for splitting encoding vectors.
            // Otherwise, the old split may become invalid.
            else if (m_encodings.size() == 1) {
                // To make things easier, always split at an index that is a multiple of m_cpu_threads.
                m_brick_idx_to_enc_vector = brick_index;
                uint32_t split_encoding_count = (brick_index_count - 1u) / m_brick_idx_to_enc_vector + 1u;
                // Start new encoding vector.
                m_encodings.back().shrink_to_fit();
                m_encodings.emplace_back();
                m_encodings.back().reserve(reserved_size);
                old_encoding_size = 0ul;
                new_encoding_size = encoded_element_count_prefix_sum[m_cpu_threads - 1] + encoded_element_count[m_cpu_threads - 1];
            } else {
                Logger(Warn) << "Brick index to encoding vector mapping is underestimating sizes.";
            }
        }

        // append the results
        m_encodings.back().resize(new_encoding_size);
#pragma omp parallel num_threads(m_cpu_threads) default(none) shared(brick_index, encoded_element_count, encoded_element_count_prefix_sum, encodedBrick, old_encoding_size)
        for (int thread_id = 0; thread_id < m_cpu_threads; thread_id++) {
            if (encoded_element_count[thread_id] == 0u)
                continue;

            assert((brick_index + thread_id) / m_brick_idx_to_enc_vector == m_encodings.size() - 1 && "Writing brick encoding to false split encoding array.");

            // store the start index of the brick within the encoding array
            m_brick_starts[brick_index + thread_id] = static_cast<uint32_t>(old_encoding_size + encoded_element_count_prefix_sum[thread_id]);
            // copy the encoded brick to the current encoding array. std::copy is sometimes slightly faster than memcpy.
            std::copy(encodedBrick[thread_id].begin(), encodedBrick[thread_id].begin() + encoded_element_count[thread_id],
                      m_encodings.back().begin() + static_cast<long>(old_encoding_size + encoded_element_count_prefix_sum[thread_id]));
        }

        // The first brick start of an encoding array is zero per default. Instead of zero, we store the total size of
        // the previous split encoding vector. This way, brick_starts[i + 1] - brick_starts[i] still yields the size of
        // the encoding of the last brick i in the previous split encoding.
        // Note that we have to handle the special case of brick_starts[j] = 0 for any brick at the start of a split
        // vector. An easy check for this case is brick_starts[j] > brick_starts[j+1].
        if (m_encodings.size() > 1 && old_encoding_size == 0ul) {
            m_brick_starts[brick_index] = static_cast<uint32_t>(m_encodings[m_encodings.size() - 2].size());
        }

        // update the maximum palette size
        for (int thread_id = 0; thread_id < m_cpu_threads; thread_id++) {
            if (encoded_element_count[thread_id] > 0u && encodedBrick[thread_id][m_encoder->getPaletteSizeHeaderIndex()] > m_max_brick_palette_count) {
                m_max_brick_palette_count = encodedBrick[thread_id][m_encoder->getPaletteSizeHeaderIndex()];
            }
        }

        // output a progress update
        if (verbose) {
            bricks_since_last_update += m_cpu_threads;
            constexpr const double PROGRESS_UPDATE_INTERVAL = 2.;
            if (progressTimer.elapsed() >= PROGRESS_UPDATE_INTERVAL) {
                float bricks_per_second = static_cast<float>(bricks_since_last_update / progressTimer.elapsed());
                uint32_t last_brick_index = glm::min(brick_index + m_cpu_threads - 1, brick_index_count);
                float remaining_seconds = static_cast<float>(brick_index_count - last_brick_index) / bricks_per_second;
                std::stringstream stream;
                stream << getLabel() << " Compression Progress " << std::fixed << std::setprecision(1) << static_cast<float>(last_brick_index) / static_cast<float>(brick_index_count) * 100.f << "%"
                       << " (" << std::setprecision(2) << (bricks_per_second * static_cast<float>(m_brick_size * m_brick_size * m_brick_size) / 1000000.f)
                       << " million voxels/second), remaining: " << static_cast<int>(remaining_seconds / 60.f) << "m" << (static_cast<int>(remaining_seconds) % 60) << "s";
                Logger(Info, true) << stream.str();
                progressTimer.restart();
                bricks_since_last_update = 0;
            }
        }

        // Our brickStarts-Array stores start positions as indices within the uint32_t encoding array.
        // If there are more than 2^32 uints in there, we can't store the start position.
        // Set a lower m_enc_vector_limit value to split the encoding into more, smaller arrays.
        if (m_encodings.back().size() > UINT32_MAX)
            throw std::runtime_error("Compressed Segmentation Volume size exceeds 32 bit address space!");
    }

    // one last dummy entry to be able to query an "end" index for the last brick
    m_brick_starts[brick_index_count] = static_cast<uint32_t>(m_encodings.back().size());

    m_last_total_encoding_seconds = static_cast<float>(totalTimer.elapsed());
    Logger(Info) << getLabel() << " Compression Progress 100% in " << std::fixed << std::setprecision(3) << m_last_total_encoding_seconds << "s (" << (static_cast<float>(volume.size()) / m_last_total_encoding_seconds / 1000000.f) << " million voxels/second) " << getEncodingInfoString();

    assert(verifyCompression() && "Compression did produce invalid encodings.");
}

// #define NO_BRICK_DECODE_INDEX_REMAP

void CompressedSegmentationVolume::decompressLOD(int target_lod, std::vector<uint32_t> &out) const {
    const glm::uvec3 brickCount = getBrickCount();
    int inv_lod = static_cast<int>(getLodCountPerBrick()) - 1 - target_lod;
    assert(inv_lod >= 0);
    if (m_random_access)
        Logger(Warn) << "Call parallelDecompressLOD() for CSGV that are compressed with random access enabled.";

    // this would run in parallel on the GPU later!
    glm::uvec3 brick_pos;
#ifndef NO_BRICK_DECODE_INDEX_REMAP
    std::vector<uint32_t> brick_cache[m_cpu_threads]; // in morton order
    for (auto &bc : brick_cache)
        bc.resize(m_brick_size * m_brick_size * m_brick_size);
#else
    void *brick_cache = nullptr; // just for OpenMP
#endif
#pragma omp parallel for num_threads(m_cpu_threads) default(none) private(brick_pos) shared(brickCount, brick_cache, out, inv_lod)
    for (uint32_t z = 0; z < brickCount.z; z++) {
        unsigned int thread_id = omp_get_thread_num();
        brick_pos.z = z; // we need that for omp...
        for (brick_pos.y = 0; brick_pos.y < brickCount.y; brick_pos.y++) {
            for (brick_pos.x = 0; brick_pos.x < brickCount.x; brick_pos.x++) {
                size_t brick_idx = brick_pos2idx(brick_pos, brickCount);
#ifndef NO_BRICK_DECODE_INDEX_REMAP
                // decode brick
                m_encoder->decodeBrick(getBrickEncoding(brick_idx), getBrickEncodingLength(brick_idx),
                                       m_separate_detail ? getBrickDetailEncoding(brick_idx) : nullptr,
                                       m_separate_detail ? getBrickDetailEncodingLength(brick_idx) : 0u,
                                       brick_cache[thread_id].data(),
                                       glm::clamp(m_volume_dim - brick_pos * m_brick_size, glm::uvec3(0u),
                                                  glm::uvec3(m_brick_size)),
                                       inv_lod);

                // fill output array with decoded brick entries
                for (uint32_t i = 0; i < m_brick_size * m_brick_size * m_brick_size; i++) {
                    glm::uvec3 out_pos = brick_pos * m_brick_size + enumBrickPos(i);
                    if (glm::all(glm::lessThan(out_pos, m_volume_dim))) {
                        out[voxel_pos2idx(out_pos, m_volume_dim)] = brick_cache[thread_id][i];
                    }
                }
#else
                decodeBrick(getBrickEncoding(brick_idx), getBrickEncodingLength(brick_idx),
                            m_separate_detail ? getBrickDetailEncoding(brick_idx) : nullptr,
                            m_separate_detail ? getBrickDetailEncodingLength(brick_idx) : 0u,
                            &(out[pos2idx(brick_pos * m_brick_size, m_volume_dim)]),
                            glm::clamp(m_volume_dim - brick_pos * m_brick_size,
                                       glm::uvec3(0u), glm::uvec3(m_brick_size)),
                            inv_lod);
#endif
            }
        }
    }
}

void CompressedSegmentationVolume::decompressBrickTo(uint32_t *out, glm::uvec3 brick_pos, int inverse_lod, uint32_t *out_encoding_debug, std::vector<glm::uvec4> *out_palette_debug) const {
    const glm::uvec3 brickCount = getBrickCount();
    size_t brick_idx = brick_pos2idx(brick_pos, brickCount);
    // decode brick
    if (out_encoding_debug) {
        m_encoder->decodeBrickWithDebugEncoding(getBrickEncoding(brick_idx), getBrickEncodingLength(brick_idx),
                                                m_separate_detail ? getBrickDetailEncoding(brick_idx) : nullptr,
                                                m_separate_detail ? getBrickDetailEncodingLength(brick_idx) : 0u,
                                                out, out_encoding_debug, out_palette_debug,
                                                glm::clamp(m_volume_dim - brick_pos * m_brick_size, glm::uvec3(0u),
                                                           glm::uvec3(m_brick_size)),
                                                inverse_lod);
    } else {
        if (m_random_access) {
            m_encoder->parallelDecodeBrick(getBrickEncoding(brick_idx), getBrickEncodingLength(brick_idx),
                                           out, glm::clamp(m_volume_dim - brick_pos * m_brick_size, glm::uvec3(0u), glm::uvec3(m_brick_size)), inverse_lod);
        } else {
            m_encoder->decodeBrick(getBrickEncoding(brick_idx), getBrickEncodingLength(brick_idx),
                                   m_separate_detail ? getBrickDetailEncoding(brick_idx) : nullptr,
                                   m_separate_detail ? getBrickDetailEncodingLength(brick_idx) : 0u,
                                   out, glm::clamp(m_volume_dim - brick_pos * m_brick_size, glm::uvec3(0u), glm::uvec3(m_brick_size)), inverse_lod);
        }
    }
}

bool CompressedSegmentationVolume::testLOD(const std::vector<uint32_t> &volume, const glm::uvec3 volume_dim) const {
    assert(volume.size() == volume_dim.x * volume_dim.y * volume_dim.z && "volume size does not match dimension");

    Logger(Info) << "Running LOD compression test";

    MiniTimer timer;

    static constexpr int max_error_lines = 8;
    size_t error_count = 0; // reset for each LOD, but we return once any LOD has errors
    std::vector<uint32_t> out;
    out.resize(static_cast<size_t>(m_volume_dim.x) * static_cast<size_t>(m_volume_dim.y) * static_cast<size_t>(m_volume_dim.z));

    bool allgood = true;

    // check the LODs over increasing LOD brick size
    // we check the LAST element in each brick. Because later we may just write the LOD entry to this single element in cached bricks.
    // we skip LOD level 0, because that's already tested in the 'test' method of VolumeCompressionBase and is technically the volume without any LOD.
    int lod = 1;
    uint32_t multigrid_lod_start = m_brick_size * m_brick_size * m_brick_size;
    for (uint32_t width = 2; width <= m_brick_size; width *= 2) {
        timer.restart();
        Logger(Info, true) << "Decode LOD " << lod << " with block width " << width;
        if (m_random_access)
            parallelDecompressLOD(lod, out);
        else
            decompressLOD(lod, out);
        Logger(Info) << "Decode LOD " << lod << " with block width " << width << " in " << timer.elapsed() << "s done. Test:";
        if (volume.size() != out.size()) {
            Logger(Error) << "Compressed in and out sizes don't match";
            Logger(Error) << "skipping other LODs...";
            Logger(Info) << "-------------------------------------------------------------";
            return false;
        }

        // iterate over all bricks but only check this one LOD
        error_count = 0;
        const glm::uvec3 brickCount = getBrickCount();
        glm::uvec3 brick;
        size_t brick_idx = 0;
#pragma omp parallel for default(none) private(brick) shared(width, volume_dim, volume, out, error_count, max_error_lines, brickCount, multigrid_lod_start)
        for (uint32_t z = 0u; z < brickCount.z; z++) {
            brick.z = z;
            for (brick.y = 0u; brick.y < brickCount.y; brick.y++) {
                for (brick.x = 0u; brick.x < brickCount.x; brick.x += m_cpu_threads) {

                    // construct target multigrid for this brick (a bit efficient since we only test one level here..)
                    std::vector<MultiGridNode> multigrid;
                    constructMultiGrid(multigrid, volume, volume_dim, brick * m_brick_size, m_brick_size, false, false);

                    // check all elements of this LoD
                    glm::uvec3 pos_in_brick;
                    for (pos_in_brick.z = 0; pos_in_brick.z < m_brick_size / width; pos_in_brick.z++) {
                        for (pos_in_brick.y = 0; pos_in_brick.y < m_brick_size / width; pos_in_brick.y += width) {
                            for (pos_in_brick.x = 0; pos_in_brick.x < m_brick_size / width; pos_in_brick.x += width) {
                                if (glm::any(glm::greaterThanEqual(brick * m_brick_size + pos_in_brick * width, volume_dim)))
                                    continue;

                                uint32_t i = voxel_pos2idx(brick * m_brick_size + pos_in_brick * width, volume_dim);
                                uint32_t expected_value = multigrid[multigrid_lod_start +
                                                                    voxel_pos2idx(pos_in_brick, glm::uvec3(m_brick_size / width))]
                                                              .label;

                                if (expected_value != out[i]) {
#pragma omp critical
                                    {
                                        error_count++;
                                        if (error_count <= max_error_lines)
                                            Logger(Error) << "error at " << str(voxel_idx2pos(i, volume_dim)) << " in " << volume[i] << " != out " << out[i] << " multigrid lod start " << multigrid_lod_start;
                                        else if (error_count == max_error_lines + 1)
                                            Logger(Error) << "[...] skipping additional errors";
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }

        size_t lod_total_number_of_elements = ((volume_dim.x + width - 1) / width) * ((volume_dim.y + width - 1) / width) * ((volume_dim.z + width - 1) / width);
        Logger(Info) << "finished with " << error_count << " / " << lod_total_number_of_elements << " errors ("
                     << (100.f * static_cast<float>(error_count) / static_cast<float>(lod_total_number_of_elements)) << "%)";

        allgood &= (error_count == 0);
        lod++;
        multigrid_lod_start += (m_brick_size / width) * (m_brick_size / width) * (m_brick_size / width);
    }

    if (allgood)
        Logger(Debug) << "no errors!";
    else
        Logger(Error) << "encountered errors!";

    Logger(Info) << "-------------------------------------------------------------";
    return error_count == 0;
}

void CompressedSegmentationVolume::exportToFile(const std::string &path, bool verbose) {
    if (m_encodings.empty()) {
        Logger(Error) << "Compression was not yet computed. Call compress(..) first. Skipping.";
        return;
    }
    if (std::filesystem::exists(path)) {
        Logger(Warn) << "File " << path << " already exist. Skipping.";
        return;
    }
    try {
        // TODO: if the path is only one file in root it has no parent_path() which causes an invalid argument
        std::filesystem::create_directories(std::filesystem::path(path).parent_path());
    } catch (const std::filesystem::filesystem_error &e) {
        throw std::runtime_error("Filesystem error: could not create parent directories for path " + std::filesystem::path(path).string());
    }
    std::ofstream file(path, std::ios_base::out | std::ios::binary);
    if (!file.is_open()) {
        Logger(Error) << "Unable to open export file " << path << ". Skipping.";
        return;
    }

    // write header: 8 chars CMPSGVOL + 4 chars version number
    const char *magic_header = "CMPSGVOL";
    const char *version = "0016";
    /* VERSION HISTORY
     * 0001: initial version
     * 0002: adds booleans if RLE and rANS are used, as well as frequency tables for rANS
     * 0003: allows separating the detail buffer
     * 0004: remove RLE flag
     * 0010: paper release version
     * 0011: use encoding_mode instead of use_rANS, allow detail separation only with DOUBLE_TABLE_RANS
     * 0012: store max. brick palette size
     * 0013: split encoding buffers
     * 0014: re-ordered operation codes by occurring frequency to Parent,X,Y,Z,PaletteA,PaletteL,PaletteD
     * 0015: random access, op mask, encoders handle specialized export data like frequency tables
     * 0016: default palette delta op ('d') uses arbitrary lengths. old behavior is special op. mask bit ('d-')
     */
    file.write(magic_header, 8);
    file.write(version, 4);

    // write general info
    file.write(reinterpret_cast<char *>(&m_brick_size), sizeof(uint32_t));
    file.write(reinterpret_cast<char *>(&m_volume_dim), sizeof(glm::uvec3));
    file.write(reinterpret_cast<char *>(&m_encoding_mode), sizeof(EncodingMode));       // since 0011
    file.write(reinterpret_cast<char *>(&m_random_access), sizeof(bool));               // since 015
    file.write(reinterpret_cast<char *>(&m_max_brick_palette_count), sizeof(uint32_t)); // since 012

    file.write(reinterpret_cast<char *>(&m_op_mask), sizeof(uint32_t)); // since 015
    m_encoder->exportToFile(file);

    // mapping of brick indices to encoding arrays
    file.write(reinterpret_cast<char *>(&m_brick_idx_to_enc_vector), sizeof(uint32_t)); // since 0013
    // write brick starts buffer
    size_t size = m_brick_starts.size();
    file.write(reinterpret_cast<char *>(&size), sizeof(size_t));
    file.write(reinterpret_cast<char *>(&m_brick_starts[0]), static_cast<long>(size * sizeof(m_brick_starts[0])));
    // write number of split encoding arrays, all split encodings, and index to split array mapping
    size = m_encodings.size();
    file.write(reinterpret_cast<char *>(&size), sizeof(size_t)); // since 0013
    for (const auto &enc : m_encodings) {                        // since 0013
        size = enc.size();
        file.write(reinterpret_cast<char *>(&size), sizeof(size_t));
        file.write(reinterpret_cast<const char *>(&enc[0]), static_cast<long>(size * sizeof(enc[0])));
    }
    // write detail encoding if it is separated
    file.write(reinterpret_cast<char *>(&m_separate_detail), sizeof(bool)); // since 0003
    if (m_separate_detail) {                                                // since 0003
        size = m_detail_starts.size();                                      // same as brickstarts
        file.write(reinterpret_cast<char *>(&size), sizeof(size_t));
        file.write(reinterpret_cast<char *>(&m_detail_starts[0]), static_cast<long>(size * sizeof(m_detail_starts[0])));

        // write number of split encoding buffers, all split encodings, and index to split array mapping
        size = m_detail_encodings.size();
        file.write(reinterpret_cast<char *>(&size), sizeof(size_t)); // since 0013
        for (const auto &enc : m_detail_encodings) {                 // since 0013
            size = enc.size();
            file.write(reinterpret_cast<char *>(&size), sizeof(size_t));
            file.write(reinterpret_cast<const char *>(&enc[0]), static_cast<long>(size * sizeof(enc[0])));
        }
    }
    file.close();
    if (verbose)
        Logger(Debug) << "Exported Compressed Segmentation Volume to " << path;
}

bool CompressedSegmentationVolume::importFromFile(const std::string &path, bool verbose, bool verify) {
    std::ifstream fin(path, std::ios::in | std::ios::binary);
    if (!fin.is_open()) {
        if (verbose)
            Logger(Error) << "Unable to open import file " << path << ". Skipping.";
        return false;
    }

    clear();
    setLabel(std::filesystem::path(path).stem().string());

    // check header and version
    char magic_header[9];
    char _version[5];
    fin.read(reinterpret_cast<char *>(magic_header), 8);
    magic_header[8] = '\0';
    if (std::string(magic_header) != "CMPSGVOL") {
        Logger(Error) << "File " << path << " is not a Compressed Segmentation Volume export. Missing header CMPSGVOL (is " << magic_header << "). Skipping.";
        return false;
    }
    fin.read(reinterpret_cast<char *>(_version), 4);
    _version[4] = '\0';
    int _numeric_version = std::stoi(std::string(_version));

    // backwards compatibility code:
    if (std::string(_version) != "0015" && std::string(_version) != "0016") {
        Logger(Error) << "Import does not support version " << _version << " of Compressed Segmentation Volume file " << path << ". Skipping.";
        return false;
    }

    // read the general data set info
    fin.read(reinterpret_cast<char *>(&m_brick_size), sizeof(uint32_t));
    fin.read(reinterpret_cast<char *>(&m_volume_dim), sizeof(glm::uvec3));
    fin.read(reinterpret_cast<char *>(&m_encoding_mode), sizeof(EncodingMode));
    fin.read(reinterpret_cast<char *>(&m_random_access), sizeof(bool));
    fin.read(reinterpret_cast<char *>(&m_max_brick_palette_count), sizeof(uint32_t));

    // update encoder
    fin.read(reinterpret_cast<char *>(&m_op_mask), sizeof(uint32_t));
    if (_numeric_version == 15)
        m_op_mask |= OP_USE_OLD_PAL_D_BIT; // compatibility: changed behavior of palette delta operation in 0016
    if (m_encoding_mode == NIBBLE_ENC) {
        m_encoder = std::make_unique<NibbleEncoder>(m_brick_size, m_encoding_mode, m_op_mask);
    } else if (m_encoding_mode == SINGLE_TABLE_RANS_ENC || m_encoding_mode == DOUBLE_TABLE_RANS_ENC) {
        m_encoder = std::make_unique<RangeANSEncoder>(m_brick_size, m_encoding_mode, m_op_mask);
    } else if (m_encoding_mode == WAVELET_MATRIX_ENC || m_encoding_mode == HUFFMAN_WM_ENC) {
        m_encoder = std::make_unique<WaveletMatrixEncoder>(m_brick_size, m_encoding_mode, m_op_mask);
    } else {
        throw std::runtime_error("No CSGV brick encoder for given encoding mode available.");
    }
    m_encoder->importFromFile(fin);

    fin.read(reinterpret_cast<char *>(&m_brick_idx_to_enc_vector), sizeof(uint32_t));
    // read the data directly to our members
    size_t size;
    fin.read(reinterpret_cast<char *>(&size), sizeof(size_t));
    m_brick_starts.resize(size);
    fin.read(reinterpret_cast<char *>(&m_brick_starts[0]), static_cast<long>(size * sizeof(uint32_t)));
    // read split encoding count
    fin.read(reinterpret_cast<char *>(&size), sizeof(size_t));
    m_encodings.resize(size);
    // read all single split encoding arrays
    for (auto &m_encoding : m_encodings) {
        fin.read(reinterpret_cast<char *>(&size), sizeof(size_t));
        m_encoding.resize(size);
        fin.read(reinterpret_cast<char *>(&m_encoding[0]), static_cast<long>(size * sizeof(uint32_t)));
    }
    // if detail is separated, read buffers
    fin.read(reinterpret_cast<char *>(&m_separate_detail), sizeof(bool));
    if (m_separate_detail) {
        fin.read(reinterpret_cast<char *>(&size), sizeof(size_t));
        m_detail_starts.resize(size);
        if (m_detail_starts.size() != m_brick_starts.size())
            throw std::runtime_error("error importing file: brickstarts and detailstarts buffers must have equal size");
        fin.read(reinterpret_cast<char *>(&m_detail_starts[0]), static_cast<long>(size * sizeof(uint32_t)));

        fin.read(reinterpret_cast<char *>(&size), sizeof(size_t));
        m_detail_encodings.resize(size);
        // read all single split encoding arrays
        for (int i = 0; i < m_detail_encodings.size(); i++) {
            fin.read(reinterpret_cast<char *>(&size), sizeof(size_t));
            m_detail_encodings[i].resize(size);
            fin.read(reinterpret_cast<char *>(&m_detail_encodings[i][0]), static_cast<long>(size * sizeof(uint32_t)));
        }
    } else {
        m_detail_starts.clear();
        m_detail_encodings.clear();
    }

    char single_byte;
    fin.read(&single_byte, 1);
    if (verbose && !fin.eof())
        Logger(Warn) << "Unexpected end of file during Compressed Segmentation Volume import!";
    fin.close();
    if (verbose)
        Logger(Debug) << "Imported Compressed Segmentation Volume from " << path << " with " << str(m_volume_dim)
                      << " = " << (static_cast<size_t>(m_volume_dim.x) * m_volume_dim.y * m_volume_dim.z)
                      << " voxels and " << getNumberOfUniqueLabelsInVolume() << " unique labels,"
                      << " encoded in " << str(getBrickCount()) << " = " << getBrickIndexCount() << " bricks"
                      << " [b=" << m_brick_size << ",e=" << EncodingMode_STR(m_encoding_mode) << "]"
                      << (isUsingSeparateDetail() ? " with seperated detail LoD" : "");

    if (verify) {
        Logger(Debug, true) << "verifying..";
        MiniTimer verifyTimer;
        if (!verifyCompression()) {
            Logger(Debug) << "verifying: FAILURE (" << verifyTimer.elapsed() << "s)";
            return false;
        } else {
            Logger(Debug) << "verifying: ok (" << verifyTimer.elapsed() << "s)";
            return true;
        }
    }
    return true;
}

void CompressedSegmentationVolume::compressForFrequencyTable(const std::vector<uint32_t> &volume, glm::uvec3 volume_dim, size_t freq_out[32], uint32_t subsampling_factor, bool detail_freq, bool verbose) {
    // check brick size
    // use a default brick size of 32 if nothing was configured for this pass before
    if (m_brick_size == 0u)
        m_brick_size = 32u;
    assert(std::popcount(m_brick_size) == 1u && "brick size must be a power of 2 > 0");

    // the frequency pass is carried out over plain 4 bit operation encodings
    EncodingMode old_rANS_mode = m_encoding_mode;
    m_encoding_mode = NIBBLE_ENC;

    m_volume_dim = volume_dim;
    glm::uvec3 brickCount = getBrickCount();
    if (verbose) {
        Logger(Info) << " running with " << m_cpu_threads << " threads on " << std::thread::hardware_concurrency() << " CPU cores";
        Logger(Info) << " brick count: " << str(brickCount) << " = " << getBrickIndexCount() << " with brick size " << m_brick_size << "^3";
    }

    Logger(Info, true) << " " << getLabel() << " Prepass Progress 0.0%";
    MiniTimer progressTimer;
    MiniTimer totalTimer;
    int bricks_since_last_update = 0;

    // compute the next m_cpu_threads brick encodings in parallel
    size_t brick_freq[m_cpu_threads][32]; // last 16 elements are detail frequencies, if detail separation is used
    // std::vector<uint32_t> tmpBrick[m_cpu_threads];
    for (int thread_id = 0; thread_id < m_cpu_threads; thread_id++) {
        for (int i = 0; i < 32; i++) {
            brick_freq[thread_id][i] = 0ul;
        }
    }

    glm::uvec3 brick;
    size_t brick_idx = 0;
    for (brick.z = 0u; brick.z < brickCount.z; brick.z += subsampling_factor) {
        for (brick.y = 0u; brick.y < brickCount.y; brick.y += subsampling_factor) {
            for (brick.x = 0u; brick.x < brickCount.x; brick.x += (subsampling_factor * m_cpu_threads)) {

#pragma omp parallel num_threads(m_cpu_threads) default(none) shared(brick, brickCount, volume, brick_freq, subsampling_factor, detail_freq)
                {
                    unsigned int thread_id = omp_get_thread_num();
                    if (brick.x + thread_id * subsampling_factor < brickCount.x) {
                        // compress the current brick
                        if (m_random_access) {
                            m_encoder->freqEncodeBrickForRandomAccess(volume, brick_freq[thread_id],
                                                                      glm::uvec3(brick.x + thread_id * subsampling_factor,
                                                                                 brick.y, brick.z) *
                                                                          m_brick_size,
                                                                      m_volume_dim, detail_freq);
                        } else {
                            m_encoder->freqEncodeBrick(volume, brick_freq[thread_id],
                                                       glm::uvec3(brick.x + thread_id * subsampling_factor,
                                                                  brick.y, brick.z) *
                                                           m_brick_size,
                                                       m_volume_dim, detail_freq);
                        }
                    }
                }

                // output a progress update
                bricks_since_last_update += m_cpu_threads;
                constexpr const double PROGRESS_UPDATE_INTERVAL = 2.;
                if (progressTimer.elapsed() >= PROGRESS_UPDATE_INTERVAL) {
                    float bricks_per_second = static_cast<float>(bricks_since_last_update / progressTimer.elapsed());
                    std::stringstream stream;
                    stream << " " << getLabel() << " Prepass Progress " << std::fixed << std::setprecision(1)
                           << static_cast<float>(brick_idx) / static_cast<float>(getBrickIndexCount() / subsampling_factor / subsampling_factor / subsampling_factor) * 100.f << "%"
                           << " (" << std::setprecision(2)
                           << (bricks_per_second * m_brick_size * m_brick_size * m_brick_size / 1000000.f)
                           << " million voxels/second)";
                    Logger(Info, true) << stream.str();
                    progressTimer.restart();
                    bricks_since_last_update = 0;
                }
            }
        }
    }

// sum up the frequencies
#pragma omp parallel for default(none) shared(freq_out, brick_freq, subsampling_factor)
    for (int i = 0; i < 32; i++) {
        freq_out[i] = 0ul;
        for (int thread_id = 0; thread_id < m_cpu_threads; thread_id++) {
            freq_out[i] += brick_freq[thread_id][i];
        }
        // scale up the values for the missing bricks
        freq_out[i] *= subsampling_factor * subsampling_factor * subsampling_factor;
    }

    // prevent accidentally counting a zero frequency for rare symbols due to subsampling
    // depending on the operation mask, different operation integers are possible:
    constexpr uint32_t op_for_opmask[] = {OP_PARENT_BIT | OP_PALETTE_D_BIT,
                                          OP_NEIGHBORX_BIT | OP_PALETTE_D_BIT,
                                          OP_NEIGHBORY_BIT | OP_PALETTE_D_BIT,
                                          OP_NEIGHBORZ_BIT | OP_PALETTE_D_BIT,
                                          OP_ALL | OP_PALETTE_D_BIT,
                                          OP_PALETTE_LAST_BIT | OP_PALETTE_D_BIT,
                                          OP_PALETTE_D_BIT,
                                          OP_PALETTE_D_BIT};
    if (subsampling_factor > 1u) {
        std::vector<int> changed_symbols = {};
        for (int i = 0; i < 8; i++) {
            // base levels freq:
            if (freq_out[i] == 0ul && (op_for_opmask[i] & m_op_mask)) {
                changed_symbols.push_back(i);
                freq_out[i] = 1ul;
            }
            // base levels freq for stop bits (and with delta values for PALETTE_DELTA operation):
            if (freq_out[i + 8] == 0ul && (op_for_opmask[i] & m_op_mask) && (m_op_mask & (OP_PALETTE_D_BIT | OP_STOP_BIT))) {
                changed_symbols.push_back(i + 8);
                freq_out[i + 8] = 1ul;
            }
            // detail freq: (no stop bits possible)
            if (detail_freq && freq_out[i + 16] == 0ul && (op_for_opmask[i] & m_op_mask)) {
                changed_symbols.push_back(i + 16);
                freq_out[i + 16] = 1ul;
            }
            // detail freq values >= 8 only for delta values in palette delta
            if (detail_freq && freq_out[i + 24] == 0ul && (m_op_mask & OP_PALETTE_D_BIT)) {
                changed_symbols.push_back(i + 24);
                freq_out[i + 24] = 1ul;
            }
        }
        if (!changed_symbols.empty()) {
            std::ranges::sort(changed_symbols);
            Logger(Debug) << " set symbol freq. for " << array_string(changed_symbols.data(), changed_symbols.size())
                          << " from 0 to 1 to avoid missing symbols due to frequency pass subsampling.";
        }
    }

    // reset rANS mode to previously configured value
    m_encoding_mode = old_rANS_mode;

    float total_seconds = static_cast<float>(totalTimer.elapsed());
    m_last_total_freq_prepass_seconds = total_seconds;
    if (verbose) {
        Logger(Info) << " " << getLabel() << " Prepass Progress 100% in " << std::fixed << std::setprecision(3)
                     << total_seconds << "s operation freq: " << arrayToString(freq_out, 16) << " | "
                     << arrayToString(freq_out + 16, 16);
    } else {
        Logger(Info) << " " << getLabel() << " Prepass Progress 100% in "
                     << std::fixed << std::setprecision(3) << total_seconds << "s";
    }
}

std::vector<uint32_t> CompressedSegmentationVolume::getCurrentFrequencyTable() const {
    if (!isUsingRANS())
        throw std::runtime_error("Can't get a frequency table from a Compressed Segmentation Volume that's not using rANS!");
    return reinterpret_cast<RangeANSEncoder *>(m_encoder.get())->getCurrentFrequencyTable();
}

std::vector<uint32_t> CompressedSegmentationVolume::getCurrentDetailFrequencyTable() const {
    if (!isUsingDetailFreq())
        throw std::runtime_error("Cannot get a detail frequency table from a Compressed Segmentation Volume that is"
                                 " not using rANS in double table mode.");
    return reinterpret_cast<RangeANSEncoder *>(m_encoder.get())->getCurrentDetailFrequencyTable();
}

}; // namespace volcanite
