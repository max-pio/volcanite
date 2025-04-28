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

#include <filesystem>
#include <fstream>
#include <map>
#include <omp.h>
#include <span>
#include <thread>
#include <unordered_set>
#include <volcanite/eval/EvaluationLogExport.hpp>

#include "VolumeCompressionBase.hpp"
#include "csgv_constants.incl" // in data/shader/cpp_glsl_include
#include "volcanite/compression/encoder/CSGVBrickEncoder.hpp"

#include "vvv/util/util.hpp"

using namespace vvv;

namespace volcanite {

// COMPRESSION
//
//    ────────────┐
//   /     /    / |
//  ┌─────┬─────┐ |
//  │ B2  │ B3  │ |   Volume is divided into uniform BRICKs with a fixed power of 2 size, e.g. 16x16x16 voxels.
//  │     │     │/|   Each BRICK is compressed/decompressed independently.
//  ├─────┼─────┤ |   Each BRICK has a hierarchical LOD structure, from coarse (1 element) to fine (brick_size^3) elements.
//  │ B0  │ B1  │ |   In coarse levels, multiple entries of a BRICK are assigned to the same value and form a multigrid node.
//  │     │     │/
//  └─────┴─────┘
//
//  The COMPRESSED array contains all encoded bricks back to back as a stream of operations along a 3D Z-Curve
//  from the coarsest to the finest LOD.
//  The BRICK_STARTS array contains an index - or pointer - to its start in COMPRESSED.
//
// ┌────┬────┬────┬────┬────────┐
// │ B0 │ B1 │ B2 │ B3 │ ...    │
// └─┬──┴─┬──┴────┴────┴────────┘
//   │    │
//   │    └─────┐
//   ▼          ▼
// ┌──────────────┬───────────────┬────────────────────────────────────────────────────────────────────────────┐
// │compressed B0 │ compressed B1 │ ...                                                     compressed last Bn │
// └┬────────────┬┴───────────────┴────────────────────────────────────────────────────────────────────────────┘
//  │            │
//  │            └────────────────────────────────────────────────────────────────────┐
//  │                                                                                 │
//  │                                                                                 │
//  │                                                                                 │
//  ├───────┬───────────────┬───────────────────┬─────────────────────┬───────────────┴┐
//  │header │ LOD_n entries │ LOD_(n-1) entries │ ...  LOD_0 entries  │ reverse palette│
//  └───────┴───────────────┴───────────────────┴─────────────────────┴────────────────┘
//
//  Each encoded BRICK contains a header with information like the (local) start positions of all LODs within the brick.
//  After the header follow all compressed LODs starting the coarsest level containing 1 element for the whole BRICK.
//  At the end, the palette for the BRICK is added in reverse order.
//
//  ────────────────────────────────────────────────────────────────────────────────────────────────────────────
//
// DECOMPRESSION
//
//  Each BRICK can be decompressed (and compressed) independently from the others.
//  For decompressing an LOD, all previous LODs have to be decompressing first, in order.
//  When a coarse LOD is decoded, the value for each multi grid node is written to the first output entry spanned by this node.
//  If a multi grid node would lie completely outside of the volume, i.e. its first entry is outside, it is skipped.
//  Note that such nodes are also skipped in the compression and have no entry in COMPRESSED.
//  Note also that on the finest LOD_0, the LOD BLOCKs are exactly one volume element large.
//
//  ────────────────────────────────────────────────────────────────────────────────────────────────────────────
//
// DETAIL SEPARATION
//
//  For enabling streaming of very large data sets to the GPU, that exceed the GPU memory limit even in compressed form,
//  we separate the so called "detail" - the operation stream of the finest LOD - from the rest of the compression.
//  The detail buffer can be seen as a separate encoding buffer with only one LoD.
//  The original encoding buffer then only contains lod_count - 1 LoDs.
//
class CompressedSegmentationVolume : public VolumeCompressionBase {

    friend class CompSegVolHandler;
    friend class CSGVChunkMerger;

  private:
    /// @return encoding array that contains the encoding of the given 1D brick index.
    [[nodiscard]] const std::vector<uint32_t> *getEncodingBufferForBrickIdx(uint32_t brick_idx) const {
        if (m_encodings.empty())
            throw std::runtime_error("Volume must be compressed first! Call compress() or import a CSGV from a file!");
        return &m_encodings.at(brick_idx / m_brick_idx_to_enc_vector);
    }
    /// @return the start uint32_t index of this brick brick_idx within the array returned by getEncodingBufferForBrickIdx(brick_idx).
    [[nodiscard]] uint32_t getBrickStart(uint32_t brick_idx) const {
        if (m_encodings.empty())
            throw std::runtime_error("Volume must be compressed first! Call compress() or import a CSGV from a file!");
        assert(brick_idx < getBrickIndexCount() && "out of bounds brick_idx");
        // Check if this is the first brick in a later split encoding array. In that case the brick start stores the
        // size of the previous encoding array instead of the actual start index 0.
        if (m_brick_starts[brick_idx] > m_brick_starts[brick_idx + 1u])
            return 0u;
        else
            return m_brick_starts[brick_idx];
    }
    /// @return the last uint32_t index of this brick brick_idx within the array returned by getEncodingBufferForBrickIdx(brick_idx).
    [[nodiscard]] uint32_t getBrickEnd(uint32_t brick_idx) const {
        if (m_encodings.empty())
            throw std::runtime_error("Volume must be compressed first! Call compress() or import a CSGV from a file!");
        assert(brick_idx < getBrickIndexCount() && "out of bounds brick_idx");
        return m_brick_starts[brick_idx + 1u];
    }

    /// @return detail encoding array that contains the separated detail encoding of the given 1D brick index.
    [[nodiscard]] const std::vector<uint32_t> *getDetailEncodingBufferForBrickIdx(uint32_t brick_idx) const {
        if (m_encodings.empty())
            throw std::runtime_error("Volume must be compressed first! Call compress() or import a CSGV from a file!");
        if (!m_separate_detail)
            throw std::runtime_error("Detail buffers not separated! Call separateDetail() first.");
        return &m_detail_encodings.at(brick_idx / m_brick_idx_to_enc_vector);
    }
    /// @return the start uint32_t index of this brick brick_idx within the array returned by getEncodingBufferForBrickIdx(brick_idx).
    [[nodiscard]] uint32_t getBrickDetailStart(uint32_t brick_idx) const {
        if (m_encodings.empty())
            throw std::runtime_error("Volume must be compressed first! Call compress() or import a CSGV from a file!");
        if (!m_separate_detail)
            throw std::runtime_error("Detail buffers not separated! Call separateDetail() first.");
        assert(brick_idx < getBrickIndexCount() && "out of bounds brick_idx");
        // Check if this is the first brick in a later split encoding array. In that case the brick start stores the
        // size of the previous encoding array instead of the actual start index 0.
        if (m_detail_starts[brick_idx] > m_detail_starts[brick_idx + 1u])
            return 0u;
        else
            return m_detail_starts[brick_idx];
    }
    /// @return the last uint32_t index of this brick brick_idx within the array returned by getDetailEncodingBufferForBrickIdx(brick_idx).
    [[nodiscard]] uint32_t getBrickDetailEnd(uint32_t brick_idx) const {
        if (m_encodings.empty())
            throw std::runtime_error("Volume must be compressed first! Call compress() or import a CSGV from a file!");
        if (!m_separate_detail)
            throw std::runtime_error("Detail buffers not separated! Call separateDetail() first.");
        assert(brick_idx < getBrickIndexCount() && "out of bounds brick_idx");
        return m_detail_starts[brick_idx + 1u];
    }

  public:
    /// Moves the detail encoding stream from each brick to the detail buffer. The detail starts buffer contains the start positions of such detail encodings afterwards.
    /// This has no effect on compression rates, but is usually only necessary when using detail level CPU to GPU streaming for rendering very large data sets.
    /// If split encodings are used, the size of the base encoding buffers shrinks below the target size due to the missing detail.
    /// The same brick index to split encoding mapping as from the base encodings is used for the separated detail encodings as well.
    /// @return the size of detail encoding / total encoding as a ratio between zero and one.
    float separateDetail();

  public:
    explicit CompressedSegmentationVolume() : VolumeCompressionBase(), m_brick_size(0u), m_encodings(), m_brick_idx_to_enc_vector(~0u), m_brick_starts(), m_detail_encodings(), m_detail_starts(), m_volume_dim(-1),
                                              m_encoding_mode(NIBBLE_ENC), m_separate_detail(false), m_cpu_threads(std::thread::hardware_concurrency()), m_max_brick_palette_count(0) {}

    virtual ~CompressedSegmentationVolume() { clear(); }

    /// Specifies the number of CPU threads to parallelize CPU computations.
    /// A value of 0 sets a count equal to the hardware concurrency.
    void setCPUThreadCount(uint32_t thread_count = 0u) {
        uint32_t hardware_concurrency = std::thread::hardware_concurrency();
        if (thread_count > hardware_concurrency)
            Logger(Warn) << "setting thread count of " << thread_count << " > hardware concurrency of " << hardware_concurrency;

        if (thread_count == 0u)
            m_cpu_threads = hardware_concurrency;
        else
            m_cpu_threads = thread_count;

        if (m_encoder)
            m_encoder->setCPUThreadCount(m_cpu_threads);
    }

    /// Performs a pseudo compression pass to obtain operation frequency tables for later rANS encoding.
    /// @param detail_freq if true, a separate table is computed for the finest LoD.
    /// @param freq_out first 16 entries contain the base frequencies. If detail_freq, the latter 16 elements contain the table for the finest LoD.
    /// @param subsampling_factor if > 1, only every other brick is handled. The higher the factor, the fewer bricks are processed.
    void compressForFrequencyTable(const std::vector<uint32_t> &volume, glm::uvec3 volume_dim, size_t freq_out[32], uint32_t subsampling_factor, bool detail_freq, bool verbose = false);

    /// Compresses the given volume for the previously set compression options and stores the result internally.
    /// Afterwards, single bricks or the full volume can be decoded to any LoD.
    /// @param volume original label volume with 32 bit labels and voxels stored in XYZ linearized order [X0Y0Z0, X1Y0Z0, ...].
    /// @param volume_dim dimensions in voxels of the original volume.
    void compress(const std::vector<uint32_t> &volume, glm::uvec3 volume_dim, bool verbose) override;

    /// Decompresses the full volume up to a certain LoD into the vector out.
    void decompressLOD(int target_lod, std::vector<uint32_t> &out) const;

    /// Decompresses the full volume up to a certain LoD into the vector out, parallelizing over the output voxels in the bricks.
    /// Only available with parallel_decode enabled.
    void parallelDecompressLOD(int target_lod, std::vector<uint32_t> &out) const;

    [[nodiscard]] std::shared_ptr<std::vector<uint32_t>> decompress() const override {
        std::shared_ptr<std::vector<uint32_t>> out = std::make_shared<std::vector<uint32_t>>();
        out->resize(static_cast<size_t>(m_volume_dim.x) * m_volume_dim.y * m_volume_dim.z);
        if (m_random_access)
            parallelDecompressLOD(0, *out);
        else
            decompressLOD(0, *out);
        return out;
    }

    /// Decompresses a single brick to the given output buffer. Note that the voxels will be in morton order in out!
    /// Additional remapping is required if another linearization should be present.
    /// @param out buffer with at least brick_size^3 elements
    /// @param brick the brick to decompress
    /// @param inverse_lod the target inverse LoD to compress the brick to. 0 is the coarsest level containing one voxel.
    void decompressBrickTo(uint32_t *out, glm::uvec3 brick, int inverse_lod, uint32_t *out_encoding_debug = nullptr, std::vector<glm::uvec4> *out_palette_debug = nullptr) const;

    /// Checks if all LOD levels are decompressed correctly. Any brick in each level should contain the max. occurring ID of all voxels within its bounds.
    [[nodiscard]] bool testLOD(const std::vector<uint32_t> &volume, glm::uvec3 volume_dim) const;

    /// Tests if the original volume can be reconstructed without errors from the encoding and if all available LoDs
    /// can be reconstructed as defined by the reference multi grids per brick.
    /// @param compress_first if true, the volume is compressed before testing.
    [[nodiscard]] bool test(const std::vector<uint32_t> &volume, const glm::uvec3 volume_dim, bool compress_first = false) override {
        if (!VolumeCompressionBase::test(volume, volume_dim, compress_first)) {
            Logger(Error) << "skipping coarser levels of detail...";
            Logger(Info) << "-------------------------------------------------------------";
            return false;
        }
        return testLOD(volume, volume_dim);
    }

    // ACCESSING FULL BUFFERS: -----------------------------------------------------------------------------------------
    /// @return vector containing all split encoding arrays.
    [[nodiscard]] const std::vector<std::vector<uint32_t>> *getAllEncodings() const {
        if (m_encodings.empty())
            throw std::runtime_error("Volume must be compressed first! Call compress() or import a CSGV from a file!");
        return &m_encodings;
    }
    /// The brick starts array contains one start index per brick counting the start uint32 element in the split
    /// encoding array of this brick. As the brick start of brick (i+1) is also used to determine the end index of
    /// brick (i), the first brick in a split encoding stores the end of the previous brick in its previous split array:\n
    /// start_i = (brickStarts[i+1] \< brickStarts[i]) ? 0u : brickStarts[i]\n
    /// end_i = brickStarts[i+1]
    /// @return vector containing start indices of all bricks within their respective split encoding array.
    [[nodiscard]] const std::vector<uint32_t> *getBrickStarts() const {
        if (m_encodings.empty())
            throw std::runtime_error("Volume must be compressed first! Call compress() or import a CSGV from a file!");
        return &m_brick_starts;
    }
    /// @return vector containing all split detail encoding arrays if detail separation is used.
    [[nodiscard]] const std::vector<std::vector<uint32_t>> *getAllDetails() const {
        if (!m_separate_detail)
            throw std::runtime_error("Detail separation must be performed before accessing detail buffers! Call separateDetail()!");
        return &m_detail_encodings;
    }
    /// The detail starts array contains one start index per brick counting the start uint32 element in the split
    /// detail encoding array of this brick. As the brick start of brick (i+1) is also used to determine the end index of
    /// brick (i), the first brick in a split encoding stores the end of the previous brick in its previous split array:\n
    /// detail_start_i = (detailStarts[i+1] \< detailStarts[i]) ? 0u : detailStarts[i]\n
    /// detail_end_i = detailStarts[i+1]
    /// @return vector containing start indices of all bricks within their respective split encoding array.
    [[nodiscard]] const std::vector<uint32_t> *getDetailStarts() const {
        if (!m_separate_detail)
            throw std::runtime_error("Detail separation must be performed before accessing detail buffers! Call separateDetail()!");
        return &m_detail_starts;
    }

    // ACCESSING SINGLE BRICKS: ----------------------------------------------------------------------------------------
    /// @return the size of the bricks encoding in number of uint32 elements.
    [[nodiscard]] uint32_t getBrickEncodingLength(uint32_t brick_idx) const {
        return getBrickEnd(brick_idx) - getBrickStart(brick_idx);
    }
    /// @return a pointer ot a continuous uint32 memory region containing this brick's encoding.
    [[nodiscard]] const uint32_t *getBrickEncoding(uint32_t brick_idx) const {
        if (brick_idx >= m_brick_starts.size() - 1)
            throw std::runtime_error("Trying to access out of bounds brick_idx " + std::to_string(brick_idx));

        assert(getBrickStart(brick_idx) + getBrickEncodingLength(brick_idx) <= getEncodingBufferForBrickIdx(brick_idx)->size() && "invalid brick encoding memory region");
        return getEncodingBufferForBrickIdx(brick_idx)->data() + getBrickStart(brick_idx);
    }
    /// @return the full brick encoding consisting of header, operation encoding, and palette as an std::span.
    [[nodiscard]] std::span<const uint32_t> getBrickEncodingSpan(uint32_t brick_idx) const {
        if (brick_idx >= m_brick_starts.size() - 1)
            throw std::runtime_error("Trying to access out of bounds brick_idx " + std::to_string(brick_idx));

        uint32_t brick_start = getBrickStart(brick_idx);
        return std::span<const uint32_t>{getEncodingBufferForBrickIdx(brick_idx)->data() + brick_start, m_brick_starts[brick_idx + 1] - brick_start};
    }
    /// @return the size of the bricks detail encoding in number of uint32 elements.
    [[nodiscard]] uint32_t getBrickDetailEncodingLength(uint32_t brick_idx) const {
        assert(isUsingSeparateDetail() && "Trying to access detail buffer length without detail separation.");
        return getBrickDetailEnd(brick_idx) - getBrickDetailStart(brick_idx);
    }
    /// @return a pointer ot a continuous uint32 memory region containing this brick's detail level encoding.
    [[nodiscard]] const uint32_t *getBrickDetailEncoding(uint32_t brick_idx) const {
        assert(isUsingSeparateDetail() && "Trying to access detail buffer without detail separation.");
        if (brick_idx >= m_brick_starts.size() - 1)
            throw std::runtime_error("Trying to access out of bounds brick_idx " + std::to_string(brick_idx));

        assert(getBrickDetailStart(brick_idx) + getBrickDetailEncodingLength(brick_idx) <= getDetailEncodingBufferForBrickIdx(brick_idx)->size() && "invalid brick detail encoding memory region");
        return getDetailEncodingBufferForBrickIdx(brick_idx)->data() + getBrickDetailStart(brick_idx);
    }
    /// @return the number of elements in the reverse palette of the brick.
    uint32_t getBrickPaletteLength(uint32_t brick_idx) const {
        if (brick_idx >= m_brick_starts.size() - 1)
            throw std::runtime_error("Trying to access out of bounds brick_idx " + std::to_string(brick_idx));
        return getBrickEncoding(brick_idx)[m_encoder->getPaletteSizeHeaderIndex()];
    }
    /// Returns the memory region containing the reverse palette of the brick.
    [[nodiscard]] std::span<const uint32_t> getBrickReversePalette(uint32_t brick_idx) const {
        if (brick_idx >= m_brick_starts.size() - 1)
            throw std::runtime_error("Trying to access out of bounds brick_idx " + std::to_string(brick_idx));
        uint32_t palette_size = getBrickPaletteLength(brick_idx);
        return std::span<const uint32_t>{
            getEncodingBufferForBrickIdx(brick_idx)->data() + m_brick_starts[brick_idx + 1] - palette_size, palette_size};
    }

    [[nodiscard]] glm::uvec3 getVolumeDim() const { return m_volume_dim; }
    [[nodiscard]] uint32_t getBrickSize() const { return m_brick_size; }
    [[nodiscard]] inline uint32_t getLodCountPerBrick() const { return glm::findMSB(m_brick_size) + 1; }
    [[nodiscard]] glm::uvec3 getBrickCount() const {
        if (m_brick_size <= 0u)
            throw std::runtime_error("Brick Size is 0");
        return (m_volume_dim - glm::uvec3(1)) / m_brick_size + 1u;
    }
    [[nodiscard]] uint32_t getBrickIndexCount() const {
        glm::uvec3 brickCount = getBrickCount();
        return brickCount.x * brickCount.y * brickCount.z;
    }
    /// Dividing any 1D brick index by the constant brickIdxToEncVector value, maps the brick index to its split
    /// encoding array index.
    [[nodiscard]] uint32_t getBrickIdxToEncVectorMapping() const { return m_brick_idx_to_enc_vector; }

    [[nodiscard]] EncodingMode getEncodingMode() const { return m_encoding_mode; }
    [[nodiscard]] bool isUsingRANS() const { return m_encoding_mode == SINGLE_TABLE_RANS_ENC || m_encoding_mode == DOUBLE_TABLE_RANS_ENC; }
    [[nodiscard]] bool isUsingDetailFreq() const { return m_encoding_mode == DOUBLE_TABLE_RANS_ENC; }
    [[nodiscard]] bool isUsingSeparateDetail() const { return m_separate_detail; }
    [[nodiscard]] bool isUsingRandomAccess() const { return m_random_access; }
    [[nodiscard]] uint32_t getOperationMask() const { return m_op_mask; }
    [[nodiscard]] bool isUsingWaveletMatrix() const { return m_encoding_mode == WAVELET_MATRIX_ENC; }

    /// returns the maximum number of uint32 palette entries that any brick in the volume contains.
    [[nodiscard]] uint32_t getMaxBrickPaletteCount() const { return m_max_brick_palette_count; };

    /// Sets the options for the compression step. If using rANS, a frequency table as a uint32_t[16] array must be given for the base.
    /// If using detail separation (use_detail) and rANS, an additional frequency table must be given for the detail buffer.
    /// @param op_mask combination of OP_*_BIT flags specifying if certain CSGV operations and stop bits are used
    /// @param random_access if true, encodes in a format that supports in-brick random access
    void setCompressionOptions(uint32_t brick_size, EncodingMode encoding_mode, uint32_t op_mask, bool random_access,
                               const uint32_t *code_frequencies = nullptr, const uint32_t *detail_code_frequencies = nullptr);

    /// Sets the options for the compression step. If using rANS, a 64 bit frequency table as a size_t[16] array must be given for the base.
    /// If an additional frequency table must be given for the finest LoD if rANS is used in double table mode.
    /// Detail separation (splitting off the operation stream of the finest LoD in a separated compressed file.
    /// @param op_mask combination of OP_*_BIT flags specifying if certain CSGV operations and stop bits are used
    /// @param random_access if true, encodes in a format that supports in-brick random access
    void setCompressionOptions64(uint32_t brick_size, EncodingMode encoding_mode, uint32_t op_mask, bool random_access,
                                 const size_t *code_frequencies = nullptr, const size_t *detail_code_frequencies = nullptr) {
        setCompressionOptions(brick_size, encoding_mode, op_mask, random_access,
                              code_frequencies ? normalizeCodeFrequencies(code_frequencies).data() : nullptr,
                              detail_code_frequencies ? normalizeCodeFrequencies(detail_code_frequencies).data() : nullptr);
    }

    ///////////////////////////////////////////////////////////////////
    ///                   file export / import                      ///
    ///////////////////////////////////////////////////////////////////
    static std::string getCSGVFileName(const std::string &filepath, uint32_t brick_size, EncodingMode rANS_mode, bool separate_detail, const std::string &filetype = ".csgv") {
        return filepath.substr(0, filepath.rfind('.')) + "_bs" + std::to_string(brick_size) + "_" + EncodingMode_ShortSTR(rANS_mode) + (separate_detail ? "_ds" : "") + filetype;
    }
    std::string getCSGVFileName(const std::string &filepath, const std::string filetype = ".csgv") {
        return getCSGVFileName(filepath, m_brick_size, m_encoding_mode, m_separate_detail, filetype);
    }
    bool importFromFile(const std::string &path, bool verbose = true, bool verify = true);
    void exportToFile(const std::string &path, bool verbose = true);

    void setLabel(const std::string &label) { m_label = label; }
    const std::string &getLabel() { return m_label; }

    void clear() {
        m_volume_dim = glm::uvec3(0u);
        m_brick_size = 0u;
        m_encodings.clear();
        m_brick_starts.clear();
        m_detail_encodings.clear();
        m_detail_starts.clear();
        m_random_access = false;
        m_op_mask = OP_ALL;
        m_separate_detail = false;
        m_brick_idx_to_enc_vector = ~0u;
        m_max_brick_palette_count = 0u;
        m_encoder = {};
    }

    size_t getCompressedSizeInBytes() const {
        size_t total_uints = 0ul;
        for (const auto &e : m_encodings)
            total_uints += e.size();
        total_uints += m_brick_starts.size() + m_detail_encodings.size() + m_detail_starts.size();
        return total_uints * sizeof(uint32_t);
    }

    /// @return the compression ratio as (compressed size) / (uncompressed uint32 volume size) in percent as a value between 0 and 100.
    float getCompressionRatio() const override {
        uint32_t label_count = getNumberOfUniqueLabelsInVolume();
        uint32_t bytes_per_voxel = getBytesForLabelCount(label_count);
        if (m_encodings.empty())
            throw std::runtime_error("CompressedSegmentationVolume must be compressed before calling getCompressionRatio()");
        return static_cast<float>(getCompressedSizeInBytes()) / static_cast<float>(m_volume_dim.x * m_volume_dim.y * m_volume_dim.z * bytes_per_voxel) * 100.f;
    }

    /// @return the number of bytes used to store an uncompressed voxel for label_count many unique labels
    static uint32_t getBytesForLabelCount(uint32_t label_count) {
        auto msb = glm::findMSB(label_count);
        if (msb > 15)
            return 4u;
        else if (msb > 7)
            return 2u;
        else
            return 1u;
    }

    /// @return multiline string describing size and compression rates of the encoded volume and encoding components.
    [[nodiscard]] std::string getEncodingInfoString() const {
        uint32_t label_count = getNumberOfUniqueLabelsInVolume();
        uint32_t bytes_per_voxel = getBytesForLabelCount(label_count);
        double brick_starts_memory = static_cast<double>(m_brick_starts.size() * sizeof(uint32_t)) * BYTE_TO_MB;
        double encoding_memory = 0.;
        for (const auto &e : m_encodings)
            encoding_memory += static_cast<double>(e.size() * sizeof(uint32_t));
        encoding_memory = encoding_memory * BYTE_TO_MB;
        double detail_starts_memory = static_cast<double>(m_detail_starts.size() * sizeof(uint32_t)) * BYTE_TO_MB;
        double detail_memory = 0.;
        for (const auto &d : m_detail_encodings)
            detail_memory += static_cast<double>(d.size() * sizeof(uint32_t)) * BYTE_TO_MB;
        double volume_memory = static_cast<double>(static_cast<size_t>(m_volume_dim[0]) * m_volume_dim[1] * m_volume_dim[2] * bytes_per_voxel) * BYTE_TO_MB;
        std::stringstream ss;
        ss << "start buffer (base  " << brick_starts_memory << "MB + detail " << detail_starts_memory
           << "MB) + encoding buffers (base " << encoding_memory << "MB + detail " << detail_memory << "MB) = "
           << (brick_starts_memory + encoding_memory + detail_starts_memory + detail_memory) << "MB / " << volume_memory
           << "MB original size (" << (static_cast<double>(brick_starts_memory + encoding_memory + detail_starts_memory + detail_memory) / volume_memory * 100.f) << "%) "
           << str(m_volume_dim) << " voxels (" << bytes_per_voxel << " byte/voxel) for " << label_count << " labels."
           << " max. brick palette size " << m_max_brick_palette_count << ".";
        if (m_encodings.size() > 1) {
            ss << "\n        Split encoding buffers (" << m_encodings.size() << "):";
            uint32_t brick_index_count = getBrickCount().x * getBrickCount().y * getBrickCount().z;
            for (int i = 0; i < m_encodings.size(); i++) {
                ss << "\n          " << static_cast<double>(m_encodings[i].size() * sizeof(uint32_t)) / 1000. / 1000. << "MB";
                if (m_separate_detail)
                    ss << " + " << static_cast<double>(m_detail_encodings[i].size() * sizeof(uint32_t)) / 1000. / 1000. << "MB detail";
                ss << ", bricks [" << (m_brick_idx_to_enc_vector * i) << " - " << std::min(m_brick_idx_to_enc_vector * (i + 1) - 1, brick_index_count) << "]";
            }
        }
        return ss.str();
    }

    ///////////////////////////////////////////////////////////////////
    ///                 statistics and evaluation                   ///
    ///////////////////////////////////////////////////////////////////

    void getBrickStatistics(std::map<std::string, float> &statistics, uint32_t brick_idx, glm::uvec3 valid_brick_size) const;
    [[nodiscard]] std::vector<std::map<std::string, float>> gatherBrickStatistics() const;

    /// Exports a human readable back-to-back list of the center brick operation stream as hex codes.
    /// The CSGV must not use any stream compression (i.e. no rANS encoding).
    void exportSingleBrickOperationsHex(const std::string &path) const;

    /// Exports back-to-back lists of brick operations to two files [path]_op.raw and [path]_op_starts.raw.\n
    /// The output depends on the compression mode. If the CSGV uses rANS:\n
    /// - op.raw contains back-to-back lists of the rANS compressed operation streams.\n
    /// - op_starts.raw stores two uint32 numbers per brick:\n
    /// If the CSGV does not use rANS:\n
    /// - op.raw stores back-to-back operation lists of the bricks using one unsigned char per operation code.\n
    /// - op_starts.raw stores two uint32 numbers per brick: the index (in 4 bit elements) of the brick's first
    /// operation and the zero-indexed position of the first operations within the brick at which the fines LoD starts.\n
    /// The op_starts.raw ends with one last dummy entry containing the total size of entries on op.raw and a zero.
    void exportAllBrickOperations(const std::string &path) const;
    void exportBrickOperationsToCSV(const std::string &path, uint32_t brick_idx) const;

    static std::vector<glm::uvec4> createBrickPosBuffer(uint32_t brick_size);

    /// Time needed for the full compression pass (without the freq. pre-pass) in seconds.
    float getLastTotalEncodingSeconds() const { return m_last_total_encoding_seconds; }
    /// Time needed for the frequency pre-pass in seconds.
    float getLastTotalFreqPrepassSeconds() const { return m_last_total_freq_prepass_seconds; }

    CSGVCompressionEvaluationResults getLastEvaluationResults() {
        uint32_t label_count = getNumberOfUniqueLabelsInVolume();
        uint32_t bytes_per_voxel = getBytesForLabelCount(label_count);
        double brick_starts_memory = static_cast<double>(m_brick_starts.size() * sizeof(uint32_t));
        double base_encoding_memory = 0.;
        for (const auto &e : m_encodings)
            base_encoding_memory += static_cast<double>(e.size() * sizeof(uint32_t));
        double detail_starts_memory = static_cast<double>(m_detail_starts.size() * sizeof(uint32_t));
        double detail_memory = 0.;
        for (const auto &d : m_detail_encodings)
            detail_memory += static_cast<double>(d.size() * sizeof(uint32_t));
        double volume_memory = static_cast<double>(static_cast<size_t>(m_volume_dim[0]) * m_volume_dim[1] * m_volume_dim[2] * bytes_per_voxel);

        CSGVCompressionEvaluationResults res;
        res.csgv_base_encoding_bytes = brick_starts_memory + base_encoding_memory;
        res.csgv_detail_encoding_bytes = detail_starts_memory + detail_memory;
        res.csgv_bytes = brick_starts_memory + base_encoding_memory + detail_starts_memory + detail_memory;
        res.compression_prepass_seconds = m_last_total_freq_prepass_seconds;
        res.compression_mainpass_seconds = m_last_total_encoding_seconds;
        res.compression_total_seconds = m_last_total_freq_prepass_seconds + m_last_total_encoding_seconds;
        res.volume_dim = m_volume_dim;
        res.volume_labels = label_count;
        res.original_volume_bytes = volume_memory;
        res.original_volume_bytes_per_voxel = static_cast<int>(bytes_per_voxel);
        res.compression_rate = res.csgv_bytes / res.original_volume_bytes;
        res.compression_GB_per_s = (res.original_volume_bytes * BYTE_TO_GB) / res.compression_total_seconds;
        return res;
    }

    ///////////////////////////////////////////////////////////////////
    ///                   rANS frequency tables                     ///
    ///////////////////////////////////////////////////////////////////
    static std::vector<uint32_t> normalizeCodeFrequencies(const size_t *freq) {
        std::vector<uint32_t> out(16);
        size_t code_freq_sum = 0ul;
        for (int i = 0; i < 16; i++) {
            code_freq_sum += freq[i];
        }
        for (int i = 0; i < 16; i++) {
            bool greaterThanZero = freq[i] > 0u;
            out[i] = static_cast<uint32_t>(freq[i] / (code_freq_sum / (1ul << 30u) + 1u));
            if (greaterThanZero && out[i] == 0u)
                out[i] = 1u; // existing symbols must not have a zero frequency
        }
        return out;
    }

    static std::vector<uint32_t> normalizeCodeFrequencies(const uint32_t *freq) {
        size_t f[16];
        for (int i = 0; i < 16; i++)
            f[i] = freq[i];
        return normalizeCodeFrequencies(f);
    }

    [[nodiscard]] std::vector<uint32_t> getCurrentFrequencyTable() const;

    [[nodiscard]] std::vector<uint32_t> getCurrentDetailFrequencyTable() const;

    [[nodiscard]] std::vector<std::string> getGLSLDefines() const {
        std::vector<std::string> shader_defines = m_encoder->getGLSLDefines([this](const uint32_t brick_idx) {
            return getBrickEncodingSpan(brick_idx);
        },
                                                                            getBrickIndexCount());
        if (isUsingRandomAccess())
            shader_defines.emplace_back("RANDOM_ACCESS");
        if (isUsingSeparateDetail())
            shader_defines.emplace_back("SEPARATE_DETAIL");
        return shader_defines;
    }

    static void printBrickInfo(glm::uvec3 brick, loglevel log_level = Info);

    void printBrickEncoding(uint32_t brick_idx) const;

    /// A quick way of checking some invariants of CSGV representations to verify the compressed volume.
    /// @return true if no errors are found, false otherwise.
    bool verifyCompression() const;

    uint32_t getNumberOfUniqueLabelsInVolume() const {
        std::vector<std::unordered_set<uint32_t>> label_set(m_cpu_threads);
// process the next m_cpu_threads bricks in parallel
#pragma omp parallel num_threads(m_cpu_threads) default(none) shared(label_set)
        {
            unsigned int thread_id = omp_get_thread_num();
            for (size_t n = thread_id; n < getBrickIndexCount(); n += m_cpu_threads) {

                if (n < getBrickIndexCount()) {
                    auto brick_encoding = getBrickEncoding(n);
                    uint32_t brick_encoding_length = getBrickEncodingLength(n);
                    uint32_t palette_size = getBrickPaletteLength(n);

                    for (int p = 1; p <= palette_size; p++) {
                        uint32_t label = brick_encoding[brick_encoding_length - p];
                        if (!label_set[thread_id].contains(label)) {
                            label_set[thread_id].insert(label);
                        }
                    }
                }
            }
        }

        // gather all thread-private label sets into one global set (the first one)
        for (int thread_id = 1; thread_id < m_cpu_threads; thread_id++) {
            for (const auto &label : label_set[thread_id]) {
                if (!label_set[0].contains(label)) {
                    label_set[0].insert(label);
                }
            }
            label_set[thread_id].clear();
        }
        return label_set[0].size();
    }

  private:
    uint32_t m_cpu_threads; ///< number of CPU threads to parallelize computations

    uint32_t m_brick_size;                          ///< brick size of each dimension in voxels, must be power of 2
    glm::uvec3 m_volume_dim;                        ///< xyz dimensions of the original volume in voxels
    std::vector<std::vector<uint32_t>> m_encodings; ///< contains all encodings for all bricks split up by brick id into several vectors
    // TODO: add user parameter to set a target size per encoding vector in MB. Pass a config struct to setCompressionOptions(..)
    uint32_t m_target_uints_per_split_encoding = 536870912u; /// targeted max. number of uint32 elements per encoding vector (536870912u -> 2 GB)
    uint32_t m_brick_idx_to_enc_vector = ~0u;                ///< dividing 1D brick idx by this value maps to split encoding vector index.
    std::vector<uint32_t> m_brick_starts;                    ///< points to indices in m_encoding
    std::vector<std::vector<uint32_t>> m_detail_encodings;   /// contains the finest LoDs of all bricks if detail separation is enabled
    std::vector<uint32_t> m_detail_starts;                   ///< points to indices m_detail_encodings

    std::unique_ptr<CSGVBrickEncoder> m_encoder = {}; ///< encodes single bricks with a certain encoding method
    EncodingMode m_encoding_mode;
    uint32_t m_op_mask = OP_ALL;  ///< if certain CSGV operations and stop bits are enabled
    bool m_random_access = false; ///< encoding supports random access within a brick

    bool m_separate_detail;
    uint32_t m_max_brick_palette_count; ///< max. palette length of any brick as a number of label entries

    // timings [s] of the last compression run (without freq. pre-pass) and the frequency pre-pass
    float m_last_total_encoding_seconds = 0.f;
    float m_last_total_freq_prepass_seconds = 0.f;
    std::string m_label = "";
};

} // namespace volcanite
