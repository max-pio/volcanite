#pragma once

#include <filesystem>
#include <fstream>
#include <omp.h>
#include <map>
#include <thread>

#include "VolumeCompressionBase.hpp"
#include "csgv_constants.h" // in data/shader/cpp_glsl_include
#include "volcanite/compression/rans.hpp"
#include "vvv/util/util.hpp"


namespace vvv {

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

private:
    // list of neighbor vectors per index (8 indices in total) where each index has 3 ivec3 vectors of type {-1, 1}^3
    static constexpr const glm::ivec3 neighbor[][3] = {{glm::ivec3({-1, 0, 0}), glm::ivec3({0, -1, 0}), glm::ivec3({0, 0, -1})},
                                                       {glm::ivec3({1, 0, 0}), glm::ivec3({0, -1, 0}), glm::ivec3({0, 0, -1})},
                                                       {glm::ivec3({-1, 0, 0}), glm::ivec3({0, 1, 0}), glm::ivec3({0, 0, -1})},
                                                       {glm::ivec3({1, 0, 0}), glm::ivec3({0, 1, 0}), glm::ivec3({0, 0, -1})},
                                                       {glm::ivec3({-1, 0, 0}), glm::ivec3({0, -1, 0}), glm::ivec3({0, 0, 1})},
                                                       {glm::ivec3({1, 0, 0}), glm::ivec3({0, -1, 0}), glm::ivec3({0, 0, 1})},
                                                       {glm::ivec3({-1, 0, 0}), glm::ivec3({0, 1, 0}), glm::ivec3({0, 0, 1})},
                                                       {glm::ivec3({1, 0, 0}), glm::ivec3({0, 1, 0}), glm::ivec3({0, 0, 1})}};

    struct ReadState {
        uint32_t idxE = 0u;             // used either as 4 bit element index or byte read index for rANS
        uint32_t rans_state = 0u;       // state of the rANS decoder
        bool in_detail_lod = false;     // if we are in the finest level-of-detail (only set in rANS double table mode)
    };

    /**
     * Reads the next element from the encoding, possibly using the rANS decoder from this CompressedSegmentationVolume, and updates the state.
     */
    uint32_t readNextLodOperation(size_t lod_start, ReadState& state) const;

    /**
     * Enumerate the positions within a brick.
     * Because of how we encode the LODs, this enumeration is required to always be in an "octree manner".
     * Iterating over it with a step size of 2*2*2=8 should land on all start points of 2x2x2 bricks in the Octree and so on.
     * Morton and Hilbert curves for example satisfy this criterion.
     */
    static inline glm::uvec3 enumBrickPos(uint32_t i, uint32_t brick_size) {
        return sfc::Morton3D::i2p(i);
    }

    static inline glm::uint32_t indexOfBrickPos(const glm::uvec3& p) {
        return sfc::Morton3D::p2i(p);
    }

    /**
     * Returns the current value in the brick at the neighbor_i neighbor position of brick_pos at the decoding stage at the given lod_width.
     * If the neighbor is not yet set in this level, the parent element of this neighbor is returned.
     * If the neighbor would lie outside the brick, UNASSIGNED is returned.
     */
    static uint32_t valueOfNeighbor(const uint32_t* brick, const glm::uvec3& brick_pos, uint32_t local_lod_i, uint32_t lod_width, uint32_t brick_size, int neighbor_i);

    static uint32_t valueOfNeighbor(const MultiGridNode* grid, const MultiGridNode* parent_grid, const glm::uvec3& brick_pos, uint32_t local_lod_i, uint32_t lod_width, uint32_t brick_size, int neighbor_i);

    /**
     * Encodes a single brick from given start with size brick_size in the volume to the out vector. out should have enough space reserved for adding all elements.
     * @return number of uint32_t elements written to out
     */
    uint32_t encodeBrick(const std::vector<uint32_t>& volume, std::vector<uint32_t>& out, glm::uvec3 start, uint32_t brick_size, glm::uvec3 volume_dim);

    /**
     * Computes operation frequencies and detail operation frequencies (the latter offset by 16) for the brick into the given brick_freq[32] array.
     */
    void freqEncodeBrick(const std::vector<uint32_t>& volume, size_t* brick_freq, glm::uvec3 start, uint32_t brick_size, glm::uvec3 volume_dim, bool detail_freq) const;

public:
//    /**
//     * @todo THIS SHOULD BE AN OPERATION ON A FILE LEVEL, AFTER A FULL COMPRESSED SEGMENTATION VOLUME IS EXPORTED WITHOUT THE SEPARATION
//     *      - reasons: compression time would be unaffected, and faster. for CPU handling, detail separation does not make sense. Cleaner code
//     *      - only the 4bit-encoding sould be split apart. the palette should remain complete in the base levels (for empty-space checking)
//     * Splits the encoding of this brick in encodedBrick with the original size encodedDetail into an encoding of the coarsest invers LoDs 0 - (g_lod_count-2) and a detail level (g_lod_count-1).
//     * The base levels are stored in the encodedBrick and their total size is stored in encoded_element_count.
//     * The detail is stored in encodedDetail and the detail level's size is stored in encoded_detail_count.
//     * The detail's memory layout is: header of a single uint32_t containing the palette size | 4bit encodings | reverse palette
//     * @param encodedBrick
//     * @param encodedDetail
//     * @param encoded_element_count
//     * @param encoded_detail_count
//     */
//    static void separateDetailLevel(std::vector<uint32_t> &encodedBrick, std::vector<uint32_t> &encodedDetail, uint32_t &encoded_element_count, uint32_t &encoded_detail_count, uint32_t lod_count);



    /**
     * Moves the detail encoding stream from each brick to the detail buffer. The detail starts buffer contains the start positions of such detail encodings afterwards.
     * This has no effect on compression rates, but is usually only necessary when using detail level CPU to GPU streaming for rendering very large data sets.
     * @return the size of detail encoding / total encoding as a ratio between zero and one
     */
    float separateDetail();

    /**
     * Encoding is the complete encoding of the data set.
     * The brick_idx is used to read the begin and endpoint of the encoding from the brick_starts buffer.
     * Output is an uint32_t array of the decoded brick. It always has to have brick_size^3 elements, but output values for elements
     * outside the volume dimension are undefined.
     *
     * @param inv_lod the LOD until which to decompress, or rather, the decompression iterations. 0 is the coarsest and log2(brick_size) is the original / finest level.
     */
    void decodeBrick(uint32_t brick_idx, uint32_t brick_size, uint32_t* output_brick, glm::uvec3 valid_brick_size, int inv_lod) const;

    // helper method to gather statistics for one single brick
    /**
     * Same as decodeBrick but also:
     * Unpacks the encoding for the given brick at a given LOD where a value of UNASSIGNED is written to octree entries/voxels that are not encoded because a STOP label occurred in a higher level.
     * The output_palette (if not nullptr) contains the values added by PALETTE_ADV in processed order as uvec4 {label, this_lod, voxel_in_brick_id, 0}
     */
    void decodeBrickWithDebugEncoding(uint32_t brick_idx, uint32_t brick_size, uint32_t* output_brick, uint32_t* output_encoding,
                                             std::vector<glm::uvec4>* output_palette, glm::uvec3 valid_brick_size, int inv_lod) const;
    void getBrickStatistics(std::map<std::string, float>& statistics, uint32_t brick_idx, glm::uvec3 valid_brick_size) const;

public:
    enum RANSMode {NO_RANS=0, SINGLE_TABLE_RANS=1, DOUBLE_TABLE_RANS=2};

    explicit CompressedSegmentationVolume() : VolumeCompressionBase(), m_brick_size(0u), m_encoding(), m_brick_starts(), m_detail_encoding(), m_detail_starts(), m_volume_dim(-1),
                                                    m_rANS_mode(NO_RANS), m_separate_detail(false), m_cpu_threads(std::thread::hardware_concurrency()) {}

    /**
     * Specifies the number of CPU threads to parallelize CPU computations.
     * A value of 0 sets a count equal to the hardware concurrency.
     */
    void setCPUThreadCount(uint32_t thread_count = 0u) {
        uint32_t hardware_concurrency = std::thread::hardware_concurrency();
        if(thread_count > hardware_concurrency)
            Logger(WARN) << "setting thread count of " << thread_count << " > hardware concurrency of " << hardware_concurrency;

        if(thread_count == 0u)
            m_cpu_threads = hardware_concurrency;
        else
            m_cpu_threads = thread_count;
    }

    /**
     * Performs a pseudo compression pass to obtain operation frequency tables for later rANS encoding.
     * @param detail_freq if true, a separate table is computed for the finest LoD.
     * @param freq_out first 16 entries contain the base frequencies. If detail_freq, the latter 16 elements contain the table for the finest LoD.
     * @param subsampling_factor if > 1, only every other brick is handled. The higher the factor, the fewer bricks are processed.
     */
    void compressForFrequencyTable(const std::vector<uint32_t>& volume, glm::uvec3 volume_dim, size_t freq_out[32], uint32_t subsampling_factor, bool detail_freq, bool verbose=false);

    /**
     * Compresses the given volume for the previously set compression options and stores the result internally.
     * Afterwards, single bricks or the full volume can be decoded to any LoD.
     * @param volume original label volume with 32 bit labels and voxels stored in XYZ linearized order [X0Y0Z0, X1Y0Z0, ...].
     * @param volume_dim dimensions in voxels of the original volume.
     */
    void compress(const std::vector<uint32_t>& volume, glm::uvec3 volume_dim, bool verbose) override;


    /**
     * Decompresses the full volume up to a certain LoD into the vector out.
     */
    void decompressLOD(int target_lod, std::vector<uint32_t>& out) const;

    std::shared_ptr<std::vector<uint32_t>> decompress() override {
        std::shared_ptr<std::vector<uint32_t>> out = std::make_shared<std::vector<uint32_t>>();
        out->resize(static_cast<size_t>(m_volume_dim.x) * m_volume_dim.y * m_volume_dim.z);
        decompressLOD(0, *out);
        return out;
    }

    /**
     * Decompresses a single brick to the given output buffer. Note that the voxels will be in morton order in out!
     * Additional remapping is required if another linearization should be present.
     * @param out buffer with at least brick_size^3 elements
     * @param brick the brick to decompress
     * @param inverse_lod the target inverse LoD to compress the brick to. 0 is the coarsest level containing one voxel.
     */
    void decompressBrickTo(uint32_t* out, glm::uvec3 brick, int inverse_lod, uint32_t* out_encoding_debug = nullptr, std::vector<glm::uvec4>* out_palette_debug = nullptr) const;

    /**
     * Checks if all LOD levels are decompressed correctly. Any brick in each level should contain the max. occurring ID of all voxels within its bounds.
     */
    bool testLOD(const std::vector<uint32_t>& volume, glm::uvec3 volume_dim) const;

    /**
     * Tests if the original volume can be reconstructed without errors from the encoding and if all available LoDs
     * can be reconstructed as defined by the reference multi grids per brick.
     * @param compress_first if true, the volume is compressed before testing
     */
    bool test(const std::vector<uint32_t>& volume, const glm::uvec3 volume_dim, bool compress_first=false) override {
        if(!VolumeCompressionBase::test(volume, volume_dim, compress_first)) {
            Logger(ERROR) << "skipping LODs...";
            Logger(INFO) << "-------------------------------------------------------------";
            return false;
        }
        return testLOD(volume, volume_dim);
    }

    std::vector<uint32_t>* getEncoding() {
        if(m_encoding.empty())
            throw std::runtime_error("Volume must be compressed first! Call compress() or import a CSGV from a file!");
        return &m_encoding;
    }
    std::vector<uint32_t>* getBrickStarts() {
        if(m_encoding.empty())
            throw std::runtime_error("Volume must be compressed first! Call compress() or import a CSGV from a file!");
        return &m_brick_starts;
    }
    std::vector<uint32_t>* getDetail() {
        if(!m_separate_detail)
            throw std::runtime_error("Detail separation must be performed before accessing detail buffers! Call separateDetail()!");
        return &m_detail_encoding;
    }
    std::vector<uint32_t>* getDetailStarts() {
        if(!m_separate_detail)
            throw std::runtime_error("Detail separation must be performed before accessing detail buffers! Call separateDetail()!");
        return &m_detail_starts;
    }
    glm::uvec3 getVolumeDim() const { return m_volume_dim; }
    glm::uint32_t getBrickSize() const { return m_brick_size; }
    uint32_t getLodCountPerBrick() const { return static_cast<uint32_t>(log2(m_brick_size)) + 1; }
    glm::uvec3 getBrickCount() const { return (m_volume_dim - glm::uvec3(1)) / m_brick_size + 1u; }
    static inline uint32_t brick_to_1D(glm::uvec3 brick_pos, glm::uvec3 brick_count) {
        return sfc::Cartesian::p2i(brick_pos, brick_count);
    }

    bool isUsingRANS() const { return m_rANS_mode == SINGLE_TABLE_RANS || m_rANS_mode == DOUBLE_TABLE_RANS; }
    bool isUsingDetailFreq() const { return m_rANS_mode == DOUBLE_TABLE_RANS; }
    bool isUsingSeparateDetail() const { return m_separate_detail; }

    /**
     * Sets the options for the compression step. If using rANS, a frequency table as a uint32_t[16] array must be given for the base.
     * If using detail separation (use_detail) and rANS, an additional frequency table must be given for the detail buffer.
     */
    void setCompressionOptions(uint32_t brick_size, RANSMode rANS_mode, const uint32_t* code_frequencies = nullptr, const uint32_t* detail_code_frequencies = nullptr) {
        if(!(brick_size > 0 && !(brick_size & (brick_size - 1))))
            throw std::runtime_error("Brick size must be a power of two greater than zero!");

        m_brick_size = brick_size;
        m_rANS_mode = rANS_mode;

        if(isUsingRANS()) {
            if(code_frequencies == nullptr)
                throw std::runtime_error("4 bit code frequencies must be given if using rANS!");
            // normalize the symbol frequencies
            std::vector<uint32_t> _code_frequencies = normalizeCodeFrequencies(code_frequencies);
            m_rans.recomputeFrequencyTables(_code_frequencies.data());
            if(isUsingDetailFreq()) {
                assert(detail_code_frequencies != nullptr && "4 bit detail code frequencies must be given if using rANS and detail separation!");
                std::vector<uint32_t> _detail_code_frequencies = normalizeCodeFrequencies(detail_code_frequencies);
                m_detail_rans.recomputeFrequencyTables(_detail_code_frequencies.data());
            }
        }
    }

    /**
     * Sets the options for the compression step. If using rANS, a 64 bit frequency table as a size_t[16] array must be given for the base.
     * If an additional frequency table must be given for the finest LoD if rANS is used in double table mode.
     * Detail separation (splitting off the operation stream of the finest LoD in a separated compressed file
     */
    void setCompressionOptions64(uint32_t brick_size, RANSMode rANS_mode, const size_t* code_frequencies = nullptr, const size_t* detail_code_frequencies = nullptr) {
        if(!(brick_size > 0 && !(brick_size & (brick_size - 1))))
            throw std::runtime_error("Brick size must be a power of two greater than zero!");
        m_brick_size = brick_size;
        m_rANS_mode = rANS_mode;

        if(isUsingRANS()) {
            if(code_frequencies == nullptr)
                throw std::runtime_error("4 bit code frequencies must be given if using rANS!");
            // normalize the symbol frequencies
            std::vector<uint32_t> _code_frequencies = normalizeCodeFrequencies(code_frequencies);
            m_rans.recomputeFrequencyTables(_code_frequencies.data());
            if(isUsingDetailFreq()) {
                assert(detail_code_frequencies != nullptr && "4 bit detail code frequencies must be given if using rANS in double table mode!");
                std::vector<uint32_t> _detail_code_frequencies = normalizeCodeFrequencies(detail_code_frequencies);
                m_detail_rans.recomputeFrequencyTables(_detail_code_frequencies.data());
            }
        }
    }

    ///////////////////////////////////////////////////////////////////
    ///                   file export / import                      ///
    ///////////////////////////////////////////////////////////////////
    static std::string getCSGVFileName(const std::string& filepath, uint32_t brick_size, RANSMode rANS_mode, bool separate_detail, const std::string& filetype= ".csgv") {
        if(separate_detail && rANS_mode != DOUBLE_TABLE_RANS)
            throw std::runtime_error("Detail separation can only be used when using rANS in double table mode!");
        std::string rANS_str = (rANS_mode == SINGLE_TABLE_RANS ? "_rANS" : (rANS_mode == DOUBLE_TABLE_RANS ? "_rANS2" : ""));
        return filepath.substr(0, filepath.rfind('.')) + "_bs" + std::to_string(brick_size) + rANS_str + (separate_detail ? "_ds" : "") + filetype;
    }
    std::string getCSGVFileName(const std::string& filepath, const std::string filetype= ".csgv") { return getCSGVFileName(filepath, m_brick_size, m_rANS_mode, m_separate_detail, filetype); }
    bool importFromFile(const std::string& path, bool verbose = true);
    void exportToFile(const std::string& path, bool verbose = true);

    void clear() {
        m_volume_dim = glm::uvec3(0u);
        m_brick_size = 0u;
        m_encoding.clear();
        m_brick_starts.clear();
        m_detail_encoding.clear();
        m_detail_starts.clear();
        m_separate_detail = false;
    }

    float getCompressedSizeInGB() {
        return static_cast<float>(m_encoding.size() + m_brick_starts.size() + m_detail_encoding.size() + m_detail_starts.size()) / 1000.f / 1000.f / 1000.f;
    }

    float getCompressionRatio() override {
        return static_cast<float>((m_encoding.size() + m_brick_starts.size() + m_detail_encoding.size() + m_detail_starts.size()) * sizeof(uint32_t)) /
               static_cast<float>(m_volume_dim.x * m_volume_dim.y * m_volume_dim.z * sizeof(uint32_t)) * 100.f;
    }

    std::string decodingInfoString() {
        double brick_starts_memory = static_cast<double>(m_brick_starts.size() * sizeof(uint32_t)) / 1000. / 1000.;
        double encoding_memory = static_cast<double>(m_encoding.size() * sizeof(uint32_t)) / 1000. / 1000.;
        double detail_starts_memory = static_cast<double>(m_detail_starts.size() * sizeof(uint32_t)) / 1000. / 1000.;
        double detail_memory = static_cast<double>(m_detail_encoding.size() * sizeof(uint32_t)) / 1000. / 1000.;
        double volume_memory = static_cast<double>(static_cast<size_t>(m_volume_dim[0]) * m_volume_dim[1] * m_volume_dim[2] * sizeof(uint32_t)) / 1000. / 1000.;
        std::stringstream ss;
        ss << "start buffer (base  " << brick_starts_memory << "MB + detail " << detail_starts_memory
           << "MB) + encoding buffer (base " << encoding_memory << "MB + detail " << detail_memory << "MB) = "
           << (brick_starts_memory + encoding_memory + detail_starts_memory + detail_memory) << "MB / " << volume_memory
           << "MB original size (" << (static_cast<double>(brick_starts_memory + encoding_memory + detail_starts_memory + detail_memory)/volume_memory*100.f) << "%) " << str(m_volume_dim) << " voxels.";
        return ss.str();
    }


    ///////////////////////////////////////////////////////////////////
    ///                 statistics and evaluation                   ///
    ///////////////////////////////////////////////////////////////////
    std::vector<std::map<std::string, float>> gatherBrickStatistics() const;

    void exportAllBrickOperations(const std::string path) const;
    void exportBrickOperationsToCSV(std::string path, uint32_t brick_idx) const;

    static std::vector<glm::uvec4> createBrickPosBuffer(uint32_t brick_size) {
        uint32_t total = brick_size * brick_size * brick_size;
        std::vector<glm::uvec4> v(total);
        for(int i = 0; i < v.size(); i++)
            v[i] = glm::uvec4(enumBrickPos(i, brick_size), 0u);
        return v;
    }

    /**
     * Time needed for the full compression pass (without the freq. pre-pass) in seconds.
     */
    float getLastTotalEncodingSeconds() { return m_last_total_encoding_seconds; }
    /**
     * Time needed for the frequency pre-pass in seconds.
     */
    float getLastTotalFreqPrepassSeconds() { return m_last_total_freq_prepass_seconds; }

    ///////////////////////////////////////////////////////////////////
    ///                   rANS frequency tables                     ///
    ///////////////////////////////////////////////////////////////////
    static std::vector<uint32_t> normalizeCodeFrequencies(const size_t* freq) {
        std::vector<uint32_t> out(16);
        size_t code_freq_sum = 0ul;
        for(int i =0; i < 16; i++) {
            code_freq_sum += freq[i];
        }
        for (int i = 0; i < 16; i++) {
            bool greaterThanZero = freq[i] > 0u;
            out[i] = static_cast<uint32_t>(freq[i] / (code_freq_sum  / (1ul << 30u) + 1u));
            if(greaterThanZero && out[i] == 0u)
                out[i] = 1u;        // existing symbols mustn't have a zero frequency
        }
        return out;
    }

    static std::vector<uint32_t> normalizeCodeFrequencies(const uint32_t* freq) {
        size_t f[16];
        for(int i = 0; i < 16; i++)
            f[i] = freq[i];
        return normalizeCodeFrequencies(f);
    }

    std::vector<uint32_t> getCurrentFrequencyTable() {
        if(!isUsingRANS())
            throw std::runtime_error("Can't get a frequency table from a Compressed Segmentation Volume that's not using rANS!");
        std::vector<uint32_t> freq(16);
        m_rans.copyCurrentFrequencyTableTo(freq.data());
        return freq;
    }

    std::vector<uint32_t> getCurrentDetailFrequencyTable() {
        if(!isUsingDetailFreq())
            throw std::runtime_error("Can't get a detail frequency table from a Compressed Segmentation Volume that's not using rANS in double table mode.");
        std::vector<uint32_t> freq(16);
        m_detail_rans.copyCurrentFrequencyTableTo(freq.data());
        return freq;
    }

    std::string getGLSLSymbolArrayStringRANS() {
        std::stringstream ss;
        ss << "uvec3[34](";
        ss << m_rans.getGLSLSymbolArrayString();
        ss << ",";
        if(m_rANS_mode == DOUBLE_TABLE_RANS) {
            ss << m_detail_rans.getGLSLSymbolArrayString();
        } else {
            // just some dummy entries so the shader compiles..
            for (int i = 0; i <= 16; i++)
                ss << (i < 16 ? "uvec3(0u, 0u, 0u)," : "uvec3(0u, 0u, 0u)");
        }
        ss << ")";
        return ss.str();
    }

    void printBrickInfo(glm::uvec3 brick, loglevel log_level = INFO) {
        if(m_encoding.empty())
            throw std::runtime_error("Segmentation volume is not yet compressed!");

        std::stringstream ss;
        uint32_t start = m_brick_starts[brick_to_1D(brick, getBrickCount())];
        uint32_t p = start;
        ss << "Brick " << str(brick) << " " << getLodCountPerBrick() << "xLoD [Header @" << p << "] LoD Starts: ";
        if(isUsingSeparateDetail()) {
            for(int i = 0; i < getLodCountPerBrick() - 1; i++) {
                ss << std::to_string(m_encoding[p++]);
                if(i < getLodCountPerBrick() - 2)
                    ss << ",";
            }
            ss << " | LoD Palette Start: ";
            for(int i = 0; i < getLodCountPerBrick() + 1; i++) {
                ss << std::to_string(m_encoding[p++]);
                if(i < getLodCountPerBrick())
                    ss << ",";
            }
            ss << " (header size " << (p - start) << ") ";
            ss << " [Encoding @" << p << "] ";
            for(int i = 0; i < std::min(8u, m_encoding[start + getLodCountPerBrick() - 2]); i++) {
                ss << m_encoding[p++] << ",";
            }
            start =  m_detail_starts[brick_to_1D(brick, getBrickCount())];
            p = start;
            ss << ".. [Detail @" << p << "] ";
            for(int i = 0; i < std::min(8u, m_detail_encoding[start]); i++) {
                ss << m_detail_encoding[p++] << ",";
            }
            ss << "..";
        }
        else {
            for(int i = 0; i < getLodCountPerBrick(); i++) {
                ss << std::to_string(m_encoding[p]);
                if(i < getLodCountPerBrick() - 1)
                    ss << ",";
                p++;
            }
            ss << " | LoD Palette Size: ";
            for(int i = 0; i < getLodCountPerBrick() + 1; i++) {
                ss << std::to_string(m_encoding[p]);
                if(i < getLodCountPerBrick())
                    ss << ",";
                p++;
            }
            ss << " (header size " << (p - start) << ") ";
            ss << " [Encoding @" << p << "] ";
            for(int i = 0; i < std::min(8u, m_encoding[start + getLodCountPerBrick() - 2]); i++) {
                ss << m_encoding[p++] << ",";
            }
        }
        Logger(log_level) << ss.str();
    }

    /** A quick way of checking some invariants of CSGV representations to verify the compressed volume. */
    bool verifyCompression() {
        if(m_encoding.empty())
            throw std::runtime_error("Segmentation volume is not yet compressed!");

        bool is_ok = true;
        glm::uvec3 brick;
        glm::uvec3 brick_count = getBrickCount();
        size_t last_brick = brick_count.x * brick_count.y * brick_count.z - 1ul;
        uint32_t lod_count = getLodCountPerBrick();
        uint32_t header_size = lod_count * 2 + (isUsingDetailFreq() ? 0 : 1);
        uint32_t header_start_lods = lod_count - (isUsingDetailFreq() ? 1 : 0);

        for(brick.z = 0u; brick.z < brick_count.z; brick.z++) {
            for (brick.y = 0u; brick.y < brick_count.y; brick.y++) {
                for (brick.x = 0u; brick.x < brick_count.x; brick.x++) {

                    std::stringstream error;
                    uint32_t brick1D = brick_to_1D(brick, getBrickCount());
                    uint32_t start = m_brick_starts[brick1D];

                    // check brick having an encoding length greater than header size + 1 operation + 1 palette entry
                    long encoding_length = static_cast<long>(m_brick_starts[brick1D + 1]) - static_cast<long>(start);
                    if(encoding_length < static_cast<long>(header_size + 1u + 1u))
                        error << " brick encoding is shorter than minimum (header size + 1 encoding + 1 palette)=" << (header_size+2) <<" but is " << encoding_length << "\n";

                    // check first header entry being header_size * 8
                    if(m_encoding[start] != header_size * 8u)
                        error << "  first encoding starts 4bit must be header*8=" << (header_size * 8u) << " but is "  << m_encoding[start] << "\n";

                    // check encoding starts being in ascending order
                    for(int l = 1; l < header_start_lods; l++) {
                        long distance = static_cast<long>(m_encoding[start + l]) - static_cast<long>(m_encoding[start + l - 1]);
                        if(distance < 0l) {
                            error << "  encoding starts are not in ascending order\n" << l << " " << distance;
                            break;
                        }
                        else if(distance > m_brick_size * m_brick_size * m_brick_size) {
                            error << "  encoding starts between LoDs are too far away\n";
                            break;
                        }
                    }

                    // check palette start of first LoD being 0 and second LoD being 1
                    if(m_encoding[start + header_start_lods] != 0u)
                        error << "  first palette start must be 0 but is " << m_encoding[start + header_start_lods];
                    if(m_encoding[start + header_start_lods + 1u] != 1u)
                        error << "  second palette start must be 0 but is " << m_encoding[start + header_start_lods + 1u];

                    // check palette starts being in ascending order
                    for(int l = 2u; l <= lod_count + 1; l++) {
                        if(m_encoding[start + header_start_lods + l] < m_encoding[start + header_start_lods + l - 1]) {
                            error << "  palette starts are not in ascending order\n";
                            break;
                        }
                    }

                    // check palette size + encoding start of last LoD being shorter than the brick encoding
                    uint32_t palette_size = m_encoding[start + header_size - 1u];
                    if(palette_size + m_encoding[start + header_start_lods]/8u > encoding_length) {
                        error << "  palette size and encoding of first (L-1) levels are longer than the total brick encoding\n";
                    }

                    // check detail encoding having at least 1 entry
                    if(isUsingSeparateDetail()) {
                        long detail_encoding_length = static_cast<long>(m_detail_starts[brick1D + 1u]) -
                                                      static_cast<long>(m_detail_starts[brick1D]);
                        if (detail_encoding_length < 1l) {
                            error << "  brick detail encoding is missing with length " << detail_encoding_length << "\n";
                        }
                    }

                    // ToDo: alter the rans.glsl shaders to handle bigger indices
                    // check for 32 Bit overflow if bytes are indexed in the buffers
                    if(glm::all(glm::equal(brick, brick_count - glm::uvec3(1)))) {
                        if (static_cast<size_t>(m_brick_starts[brick1D + 1u]) * 4ul > (~0u)) {
                            error << "  encoding contains more bytes ("
                                  << (static_cast<size_t>(m_brick_starts[brick1D + 1u]) * 4ul)
                                  << ") than 32 bit can index (" << (~0u) << ")\n";
                        }

                        if (isUsingSeparateDetail()) {
                            if (static_cast<size_t>(m_detail_starts[brick1D + 1u]) * 4ul > (~0u)) {
                                error << "  detail encoding contains more bytes ("
                                      << (static_cast<size_t>(m_detail_starts[brick1D + 1u]) * 4ul)
                                        << ") than 32 bit can index (" << (~0u) << ")\n";
                            }
                        }
                    }

                    // print error message
                    if(!error.str().empty()) {
//                        #pragma omp critical
                        {
                            Logger(ERROR) << "Found errors for brick:\n" << error.str();
                            printBrickInfo(brick, ERROR);
                        }
                        is_ok = false;
                    }

                    if(!is_ok)
                        break;
                }
                if(!is_ok)
                    break;
            }
            if(!is_ok)
                break;
        }
        return is_ok;
    }

private:
    uint32_t m_cpu_threads;                     // number of CPU threads to parallelize computations

    uint32_t m_brick_size;
    glm::uvec3 m_volume_dim;
    std::vector<uint32_t> m_encoding;           // contains all compressed entries for all bricks and each brick is capable of reconstructing all LODs, except the finest one!
    std::vector<uint32_t> m_brick_starts;       // points to indices in m_encoding
    std::vector<uint32_t> m_detail_encoding;    // contains the finest LoD
    std::vector<uint32_t> m_detail_starts;      // points to indices m_detail_encoding

    RANS m_rans;
    RANS m_detail_rans;
    RANSMode m_rANS_mode;
    bool m_separate_detail;

    // timings [s] of the last compression run (without freq. pre-pass) and the frequency pre-pass
    float m_last_total_encoding_seconds = 0.f;
    float m_last_total_freq_prepass_seconds = 0.f;
};

} // namespace vvv
