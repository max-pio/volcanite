#pragma once

#include <filesystem>
#include <fstream>
#include <omp.h>
#include <map>
#include <thread>
#include <span>

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
    static constexpr const glm::ivec3 neighbor[8][3] = {{glm::ivec3({-1, 0, 0}), glm::ivec3({0, -1, 0}), glm::ivec3({0, 0, -1})},
                                                        {glm::ivec3({ 1, 0, 0}), glm::ivec3({0, -1, 0}), glm::ivec3({0, 0, -1})},
                                                        {glm::ivec3({-1, 0, 0}), glm::ivec3({0,  1, 0}), glm::ivec3({0, 0, -1})},
                                                        {glm::ivec3({ 1, 0, 0}), glm::ivec3({0,  1, 0}), glm::ivec3({0, 0, -1})},
                                                        {glm::ivec3({-1, 0, 0}), glm::ivec3({0, -1, 0}), glm::ivec3({0, 0,  1})},
                                                        {glm::ivec3({ 1, 0, 0}), glm::ivec3({0, -1, 0}), glm::ivec3({0, 0,  1})},
                                                        {glm::ivec3({-1, 0, 0}), glm::ivec3({0,  1, 0}), glm::ivec3({0, 0,  1})},
                                                        {glm::ivec3({ 1, 0, 0}), glm::ivec3({0,  1, 0}), glm::ivec3({0, 0,  1})}};

    struct ReadState {
        uint32_t idxE = 0u;             // used either as 4 bit element index or byte read index for rANS
        uint32_t rans_state = 0u;       // state of the rANS decoder
        bool in_detail_lod = false;     // if we are in the finest level-of-detail (only set in rANS double table mode)
    };

    /** Reads the next element from the brick encoding, possibly using the rANS decoder from this CompressedSegmentationVolume, and updates the state.*/
    uint32_t readNextLodOperationFromEncoding(const uint32_t* brick_encoding, ReadState& state) const;

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

    /** @return encoding array that contains the encoding of the given 1D brick index. */
    [[nodiscard]] const std::vector<uint32_t>* getEncodingBufferForBrickIdx(uint32_t brick_idx) const {
        if(m_encodings.empty())
            throw std::runtime_error("Volume must be compressed first! Call compress() or import a CSGV from a file!");
        return &m_encodings.at(brick_idx / m_brick_idx_to_enc_vector);
    }
    /** @return the start uint32_t index of this brick brick_idx within the array returned by getEncodingBufferForBrickIdx(brick_idx). */
    [[nodiscard]] uint32_t getBrickStart(uint32_t brick_idx) const {
        if(m_encodings.empty())
            throw std::runtime_error("Volume must be compressed first! Call compress() or import a CSGV from a file!");
        assert(brick_idx < getBrickIndexCount() && "out of bounds brick_idx");
        // Check if this is the first brick in a later split encoding array. In that case the brick start stores the
        // size of the previous encoding array instead of the actual start index 0.
        if(m_brick_starts[brick_idx] > m_brick_starts[brick_idx + 1u])
            return 0u;
        else
            return m_brick_starts[brick_idx];
    }
    /** @return the last uint32_t index of this brick brick_idx within the array returned by getEncodingBufferForBrickIdx(brick_idx). */
    [[nodiscard]] uint32_t getBrickEnd(uint32_t brick_idx) const {
        if(m_encodings.empty())
            throw std::runtime_error("Volume must be compressed first! Call compress() or import a CSGV from a file!");
        assert(brick_idx < getBrickIndexCount() && "out of bounds brick_idx");
        return m_brick_starts[brick_idx + 1u];
    }

    /** @return detail encoding array that contains the separated detail encoding of the given 1D brick index. */
    [[nodiscard]] const std::vector<uint32_t>* getDetailEncodingBufferForBrickIdx(uint32_t brick_idx) const {
        if(m_encodings.empty())
            throw std::runtime_error("Volume must be compressed first! Call compress() or import a CSGV from a file!");
        if(!m_separate_detail)
            throw std::runtime_error("Detail buffers not separated! Call separateDetail() first.");
        return &m_detail_encodings.at(brick_idx / m_brick_idx_to_enc_vector);
    }
    /** @return the start uint32_t index of this brick brick_idx within the array returned by getEncodingBufferForBrickIdx(brick_idx). */
    [[nodiscard]] uint32_t getBrickDetailStart(uint32_t brick_idx) const {
        if(m_encodings.empty())
            throw std::runtime_error("Volume must be compressed first! Call compress() or import a CSGV from a file!");
        if(!m_separate_detail)
            throw std::runtime_error("Detail buffers not separated! Call separateDetail() first.");
        assert(brick_idx < getBrickIndexCount() && "out of bounds brick_idx");
        // Check if this is the first brick in a later split encoding array. In that case the brick start stores the
        // size of the previous encoding array instead of the actual start index 0.
        if(m_detail_starts[brick_idx] > m_detail_starts[brick_idx + 1u])
            return 0u;
        else
            return m_detail_starts[brick_idx];
    }
    /** @return the last uint32_t index of this brick brick_idx within the array returned by getDetailEncodingBufferForBrickIdx(brick_idx). */
    [[nodiscard]] uint32_t getBrickDetailEnd(uint32_t brick_idx) const {
        if(m_encodings.empty())
            throw std::runtime_error("Volume must be compressed first! Call compress() or import a CSGV from a file!");
        if(!m_separate_detail)
            throw std::runtime_error("Detail buffers not separated! Call separateDetail() first.");
        assert(brick_idx < getBrickIndexCount() && "out of bounds brick_idx");
        return m_detail_starts[brick_idx + 1u];
    }

    /** Returns the current value in the brick at the neighbor_i neighbor position of brick_pos at the decoding stage at the given lod_width.
     * If the neighbor is not yet set in this level, the parent element of this neighbor is returned.
     * If the neighbor would lie outside the brick, UNASSIGNED is returned. */
    static uint32_t valueOfNeighbor(const uint32_t* brick, const glm::uvec3& brick_pos, uint32_t local_lod_i, uint32_t lod_width, uint32_t brick_size, int neighbor_i);

    static uint32_t valueOfNeighbor(const MultiGridNode* grid, const MultiGridNode* parent_grid, const glm::uvec3& brick_pos, uint32_t local_lod_i, uint32_t lod_width, uint32_t brick_size, int neighbor_i);

    /** Encodes a single brick from given start with size brick_size in the volume to the out vector.
     * @param volume the labeled voxel volume to encode.
     * @param out must have enough space reserved for adding all elements.
     * @param start the start position of the brick. Should be a multiple of the configured brick size.
     * @param volume_dim the volume size in voxels in each dimension
     * @return number of uint32_t elements written to out. */
    uint32_t encodeBrick(const std::vector<uint32_t>& volume, std::vector<uint32_t>& out, glm::uvec3 start, glm::uvec3 volume_dim);

    /** Encodes a single brick from given start with size brick_size in the volume to the out vector for in-brick parallel decompression.
     * @param volume the labeled voxel volume to encode.
     * @param out must have enough space reserved for adding all elements.
     * @param start the start position of the brick. Should be a multiple of the configured brick size.
     * @param volume_dim the volume size in voxels in each dimension
     * @return number of uint32_t elements written to out */
    uint32_t encodeBrickForParallelDecode(const std::vector<uint32_t>& volume, std::vector<uint32_t>& out, glm::uvec3 start, glm::uvec3 volume_dim);


    /** Computes operation frequencies and detail operation frequencies (the latter offset by 16) for the brick into the given brick_freq[32] array. */
    void freqEncodeBrick(const std::vector<uint32_t>& volume, size_t* brick_freq, glm::uvec3 start, glm::uvec3 volume_dim, bool detail_freq) const;

    /** Computes operation frequencies and detail operation frequencies (the latter offset by 16) for the brick into the given brick_freq[32] array. */
    void freqEncodeBrickForParallelDecode(const std::vector<uint32_t>& volume, size_t* brick_freq, glm::uvec3 start, glm::uvec3 volume_dim, bool detail_freq) const;


public:

    /** Moves the detail encoding stream from each brick to the detail buffer. The detail starts buffer contains the start positions of such detail encodings afterwards.
     * This has no effect on compression rates, but is usually only necessary when using detail level CPU to GPU streaming for rendering very large data sets.
     * If split encodings are used, the size of the base encoding buffers shrinks below the target size due to the missing detail.
     * The same brick index to split encoding mapping as from the base encodings is used for the separated detail encodings as well.
     * @return the size of detail encoding / total encoding as a ratio between zero and one */
    float separateDetail();

    /** Decompresses a single brick.
     * @param brick_idx is used to read the begin and endpoint of the encoding from the brick_starts buffer.
     * @param output_brick is an uint32_t array of the decoded brick. It always has to have brick_size^3 elements.
     * @param valid_brick_size is used to clamp used voxels for border bricks. Values outside are undefined.
     * @param inv_lod the LOD until which to decompress, or rather, the decompression iterations. 0 is the coarsest and log2(brick_size) is the original / finest level. */
    void decodeBrick(uint32_t brick_idx, uint32_t* output_brick, glm::uvec3 valid_brick_size, int inv_lod) const;

    /** Decompresses a single brick in parallel.
     * @param brick_idx is used to read the begin and endpoint of the encoding from the brick_starts buffer.
     * @param output_brick is an uint32_t array of the decoded brick. It always has to have brick_size^3 elements.
     * @param valid_brick_size is used to clamp used voxels for border bricks. Values outside are undefined.
     * @param target_inv_lod the LOD until which to decompress. 0 is the coarsest and log2(brick_size) is the original / finest level. */
    void parallelDecodeBrick(uint32_t brick_idx, uint32_t* output_brick, glm::uvec3 valid_brick_size, int target_inv_lod) const;



    /** Helper method to gather statistics for one single brick. Same as decodeBrick but also:
     * Unpacks the encoding for the given brick at a given LOD where a value of INVALID is written to octree entries/voxels that are not encoded because a STOP label occurred in a higher level.
     * The output_palette (if not nullptr) contains the values added by PALETTE_ADV in processed order as uvec4 {label, this_lod, voxel_in_brick_id, 0}
     */
    void decodeBrickWithDebugEncoding(uint32_t brick_idx, uint32_t* output_brick, uint32_t* output_encoding,
                                             std::vector<glm::uvec4>* output_palette, glm::uvec3 valid_brick_size, int inv_lod) const;

    void getBrickStatistics(std::map<std::string, float>& statistics, uint32_t brick_idx, glm::uvec3 valid_brick_size) const;

public:
    explicit CompressedSegmentationVolume() : VolumeCompressionBase(), m_brick_size(0u), m_encodings(), m_brick_idx_to_enc_vector(~0u), m_brick_starts(), m_detail_encodings(), m_detail_starts(), m_volume_dim(-1),
                                              m_rANS_mode(NO_RANS), m_separate_detail(false), m_cpu_threads(std::thread::hardware_concurrency()), m_max_brick_palette_count(0) {}

    ~CompressedSegmentationVolume() { clear(); }

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


    /** Decompresses the full volume up to a certain LoD into the vector out. */
    void decompressLOD(int target_lod, std::vector<uint32_t>& out) const;

    /** Decompresses the full volume up to a certain LoD into the vector out, parallelizing over the output voxels in the bricks. */
    void parallelDecompressLOD(int target_lod, std::vector<uint32_t>& out) const;

    std::shared_ptr<std::vector<uint32_t>> decompress() override {
        std::shared_ptr<std::vector<uint32_t>> out = std::make_shared<std::vector<uint32_t>>();
        out->resize(static_cast<size_t>(m_volume_dim.x) * m_volume_dim.y * m_volume_dim.z);
        if(m_parallel_decode)
            parallelDecompressLOD(0, *out);
        else
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

    /** Checks if all LOD levels are decompressed correctly. Any brick in each level should contain the max. occurring ID of all voxels within its bounds. */
    bool testLOD(const std::vector<uint32_t>& volume, glm::uvec3 volume_dim) const;

    /** Tests if the original volume can be reconstructed without errors from the encoding and if all available LoDs
     * can be reconstructed as defined by the reference multi grids per brick.
     * @param compress_first if true, the volume is compressed before testing */
    bool test(const std::vector<uint32_t>& volume, const glm::uvec3 volume_dim, bool compress_first=false) override {
        if(!VolumeCompressionBase::test(volume, volume_dim, compress_first)) {
            Logger(ERROR) << "skipping coarser levels of detail...";
            Logger(INFO) << "-------------------------------------------------------------";
            return false;
        }
        return testLOD(volume, volume_dim);
    }

    // ACCESSING FULL BUFFERS: -----------------------------------------------------------------------------------------
    /** @return vector containing all split encoding arrays. */
    [[nodiscard]] const std::vector<std::vector<uint32_t>>* getAllEncodings() const {
        if(m_encodings.empty())
            throw std::runtime_error("Volume must be compressed first! Call compress() or import a CSGV from a file!");
        return &m_encodings;
    }
    /** The brick starts array contains one start index per brick counting the start uint32 element in the split
     * encoding array of this brick. As the brick start of brick (i+1) is also used to determine the end index of
     * brick (i), the first brick in a split encoding stores the end of the previous brick in its previous split array:\n
     * start_i = (brickStarts[i+1] \< brickStarts[i]) ? 0u : brickStarts[i]\n
     * end_i = brickStarts[i+1]
     * @return vector containing start indices of all bricks within their respective split encoding array. */
    [[nodiscard]] const std::vector<uint32_t>* getBrickStarts() const {
        if(m_encodings.empty())
            throw std::runtime_error("Volume must be compressed first! Call compress() or import a CSGV from a file!");
        return &m_brick_starts;
    }
    /** @return vector containing all split detail encoding arrays if detail separation is used. */
    [[nodiscard]] const std::vector<std::vector<uint32_t>>* getAllDetails() const {
        if(!m_separate_detail)
            throw std::runtime_error("Detail separation must be performed before accessing detail buffers! Call separateDetail()!");
        return &m_detail_encodings;
    }
    /** The detail starts array contains one start index per brick counting the start uint32 element in the split
     * detail encoding array of this brick. As the brick start of brick (i+1) is also used to determine the end index of
     * brick (i), the first brick in a split encoding stores the end of the previous brick in its previous split array:\n
     * detail_start_i = (detailStarts[i+1] \< detailStarts[i]) ? 0u : detailStarts[i]\n
     * detail_end_i = detailStarts[i+1]
     * @return vector containing start indices of all bricks within their respective split encoding array. */
     [[nodiscard]] const std::vector<uint32_t>* getDetailStarts() const {
        if(!m_separate_detail)
            throw std::runtime_error("Detail separation must be performed before accessing detail buffers! Call separateDetail()!");
        return &m_detail_starts;
    }

    // ACCESSING SINGLE BRICKS: ----------------------------------------------------------------------------------------
    /** @return the size of the bricks encoding in number of uint32 elements.*/
    [[nodiscard]] uint32_t getBrickEncodingLength(uint32_t brick_idx) const {
        return getBrickEnd(brick_idx) - getBrickStart(brick_idx);
    }
    /** @return a pointer ot a continuous uint32 memory region containing this brick's encoding.*/
    [[nodiscard]] const uint32_t* getBrickEncoding(uint32_t brick_idx) const {
        if(brick_idx >= m_brick_starts.size() - 1)
            throw std::runtime_error("Trying to access out of bounds brick_idx " + std::to_string(brick_idx));

        assert(getBrickStart(brick_idx) + getBrickEncodingLength(brick_idx) <= getEncodingBufferForBrickIdx(brick_idx)->size() && "invalid brick encoding memory region");
        return getEncodingBufferForBrickIdx(brick_idx)->data() + getBrickStart(brick_idx);
    }
    /** @return the full brick encoding consisting of header, operation encoding, and palette as an std::span.**/
    [[nodiscard]] std::span<const uint32_t> getBrickEncodingSpan(uint32_t brick_idx) const {
        if(brick_idx >= m_brick_starts.size() - 1)
            throw std::runtime_error("Trying to access out of bounds brick_idx " + std::to_string(brick_idx));

        uint32_t brick_start = getBrickStart(brick_idx);
        return std::span<const uint32_t>{getEncodingBufferForBrickIdx(brick_idx)->data() + brick_start, m_brick_starts[brick_idx + 1] - brick_start};
    }
    /** @return the size of the bricks detail encoding in number of uint32 elements.*/
    [[nodiscard]] uint32_t getBrickDetailEncodingLength(uint32_t brick_idx) const {
        return getBrickDetailEnd(brick_idx) - getBrickDetailStart(brick_idx);
    }
    /** @return a pointer ot a continuous uint32 memory region containing this brick's detail level encoding.*/
    [[nodiscard]] const uint32_t* getBrickDetailEncoding(uint32_t brick_idx) const {
        if(brick_idx >= m_brick_starts.size() - 1)
            throw std::runtime_error("Trying to access out of bounds brick_idx " + std::to_string(brick_idx));

        assert(getBrickDetailStart(brick_idx) + getBrickDetailEncodingLength(brick_idx) <= getDetailEncodingBufferForBrickIdx(brick_idx)->size() && "invalid brick detail encoding memory region");
        return getDetailEncodingBufferForBrickIdx(brick_idx)->data() + getBrickDetailStart(brick_idx);
    }
    /** @return the number of elements in the reverse palette of the brick. */
    uint32_t getBrickPaletteLength(uint32_t brick_idx) const {
        if(brick_idx >= m_brick_starts.size() - 1)
            throw std::runtime_error("Trying to access out of bounds brick_idx " + std::to_string(brick_idx));
        return getBrickEncoding(brick_idx)[getPaletteSizeHeaderIndex()];
    }
    /** Returns the memory region containing the reverse palette of the brick. */
    [[nodiscard]] std::span<const uint32_t> getBrickReversePalette(uint32_t brick_idx) const {
        if(brick_idx >= m_brick_starts.size() - 1)
            throw std::runtime_error("Trying to access out of bounds brick_idx " + std::to_string(brick_idx));
        uint32_t palette_size = getBrickPaletteLength(brick_idx);
        return std::span<const uint32_t>{
                getEncodingBufferForBrickIdx(brick_idx)->data() + m_brick_starts[brick_idx + 1] - palette_size, palette_size};
    }

    [[nodiscard]] glm::uvec3 getVolumeDim() const { return m_volume_dim; }
    [[nodiscard]] uint32_t getBrickSize() const { return m_brick_size; }
    [[nodiscard]] inline uint32_t getLodCountPerBrick() const { return static_cast<uint32_t>(log2(m_brick_size)) + 1; }
    [[nodiscard]] glm::uvec3 getBrickCount() const {
        if(m_brick_size <= 0u)
            throw std::runtime_error("Brick Size is 0");
        return (m_volume_dim - glm::uvec3(1)) / m_brick_size + 1u;
    }
    [[nodiscard]] uint32_t getBrickIndexCount() const {
        glm::uvec3 brickCount = getBrickCount();
        return brickCount.x * brickCount.y * brickCount.z;
    }
    /** Dividing any 1D brick index by the constant brickIdxToEncVector value, maps the brick index to its split encoding
     * array index. */
    [[nodiscard]] uint32_t getBrickIdxToEncVectorMapping() const { return m_brick_idx_to_enc_vector; }

    static inline uint32_t brick_pos2idx(glm::uvec3 brick_pos, glm::uvec3 brick_count) {
        return sfc::Cartesian::p2i(brick_pos, brick_count);
    }
    static inline glm::uvec3 brick_idx2pos(uint32_t brick_index, glm::uvec3 brick_count) {
        return sfc::Cartesian::i2p(brick_index, brick_count);
    }

    // parallel decode:
    /** Returns the number of operations stored in a brick (one per output voxel) when no stop bits are used. */
    inline uint32_t getMaxOperationsInBrick() const {
        return getMaxOperationsUpToInvLoD(getLodCountPerBrick() - 1u);
    }
    /** Returns the number of operations in a brick (one per output voxel) when no stop bits are used up to inv. LoD */
    inline uint32_t getMaxOperationsUpToInvLoD(uint32_t inv_lod) const {
        // ignoring stop bits:
        // a brick contains 1 operation for the coarsest LoD, 2*2*2=8 for the next LoD, 4*4*4=64 for the next loD, ...
        // For the first N inverse LoDs this results in a total number of operations of
        //     SUM_0^N (2^n)^3  = 1/7 (8^(i+1) - 1)
        return ((1u << 3u*(inv_lod+1u)) - 1u)/7u;
    }

    [[nodiscard]] RANSMode getRANSMode() const { return m_rANS_mode; }
    [[nodiscard]] bool isUsingRANS() const { return m_rANS_mode == SINGLE_TABLE_RANS || m_rANS_mode == DOUBLE_TABLE_RANS; }
    [[nodiscard]] bool isUsingDetailFreq() const { return m_rANS_mode == DOUBLE_TABLE_RANS; }
    [[nodiscard]] bool isUsingSeparateDetail() const { return m_separate_detail; }

    /** returns the maximum number of uint32 palette entries that any brick in the volume contains. **/
    [[nodiscard]] uint32_t getMaxBrickPaletteCount() const { return m_max_brick_palette_count; };
    /** returns the size of the header at the beginning of each brick measured in uint32 entries. **/
    [[nodiscard]] uint32_t getHeaderSize() const { return getLodCountPerBrick() * 2 + (isUsingSeparateDetail() ? 0 : 1); }
    /** returns the index of the uint32_t element in the brick encoding / header that stores the palette size. **/
    [[nodiscard]] uint32_t getPaletteSizeHeaderIndex() const { return getHeaderSize() - 1u; }

    /**
     * Sets the options for the compression step. If using rANS, a frequency table as a uint32_t[16] array must be given for the base.
     * If using detail separation (use_detail) and rANS, an additional frequency table must be given for the detail buffer.
     */
    void setCompressionOptions(uint32_t brick_size, RANSMode rANS_mode, bool parallel_decoding,
                               const uint32_t* code_frequencies = nullptr, const uint32_t* detail_code_frequencies = nullptr) {
        if(!m_encodings.empty()) {
            Logger(WARN) << "CompressedSegmentationVolume was already compressed. Clearing old data on new config.";
            clear();
        }

        if(!(brick_size > 0 && !(brick_size & (brick_size - 1))))
            throw std::runtime_error("Brick size must be a power of two greater than zero!");
        if(parallel_decoding && rANS_mode != NO_RANS)
            throw std::runtime_error("Parallel Decoding only works without rANS");

        m_brick_size = brick_size;
        m_rANS_mode = rANS_mode;
        m_parallel_decode = parallel_decoding;

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
    void setCompressionOptions64(uint32_t brick_size, RANSMode rANS_mode, bool parallel_decoding,
                                 const size_t* code_frequencies = nullptr, const size_t* detail_code_frequencies = nullptr) {
        if(!m_encodings.empty()) {
            Logger(WARN) << "CompressedSegmentationVolume was already compressed. Clearing old data on new config.";
            clear();
        }

        if(!(brick_size > 0 && !(brick_size & (brick_size - 1))))
            throw std::runtime_error("Brick size must be a power of two greater than zero!");
        if(parallel_decoding && rANS_mode != NO_RANS)
            throw std::runtime_error("Parallel Decoding only works without rANS");

        m_brick_size = brick_size;
        m_rANS_mode = rANS_mode;
        m_parallel_decode = parallel_decoding;

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
    bool importFromFile(const std::string& path, bool verbose = true, bool verify = true);
    void exportToFile(const std::string& path, bool verbose = true);

    const std::string& getLabel() { return m_label; }

    void clear() {
        m_volume_dim = glm::uvec3(0u);
        m_brick_size = 0u;
        m_encodings.clear();
        m_brick_starts.clear();
        m_detail_encodings.clear();
        m_detail_starts.clear();
        m_separate_detail = false;
        m_brick_idx_to_enc_vector = ~0u;
        m_max_brick_palette_count = 0u;
    }

    size_t getCompressedSizeInBytes() const {
        size_t total_uints = 0ul;
        for(const auto& e : m_encodings)
            total_uints += e.size();
        total_uints += m_brick_starts.size() + m_detail_encodings.size() + m_detail_starts.size();
        return total_uints * sizeof(uint32_t);
    }

    /** @return the compression ratio as (compressed size) / (uncompressed uint32 volume size) in percent as a value between 0 and 100. */
    float getCompressionRatio() override {
        if(m_encodings.empty())
            throw std::runtime_error("CompressedSegmentationVolume must be compressed before calling getCompressionRatio()");
        return static_cast<float>(getCompressedSizeInBytes()) / static_cast<float>(m_volume_dim.x * m_volume_dim.y * m_volume_dim.z * sizeof(uint32_t)) * 100.f;
    }

    /** @return multiline string describing size and compression rates of the encoded volume and encoding components. */
    [[nodiscard]] std::string getEncodingInfoString() const {
        double brick_starts_memory = static_cast<double>(m_brick_starts.size() * sizeof(uint32_t)) / 1000. / 1000.;
        double encoding_memory = 0.;
        for(const auto& e : m_encodings)
            encoding_memory += static_cast<double>(e.size() * sizeof(uint32_t));
        encoding_memory = encoding_memory / 1000. / 1000.;
        double detail_starts_memory = static_cast<double>(m_detail_starts.size() * sizeof(uint32_t)) / 1000. / 1000.;
        double detail_memory = 0.;
        for(const auto& d : m_detail_encodings)
            detail_memory += static_cast<double>(d.size() * sizeof(uint32_t)) / 1000. / 1000.;
        double volume_memory = static_cast<double>(static_cast<size_t>(m_volume_dim[0]) * m_volume_dim[1] * m_volume_dim[2] * sizeof(uint32_t)) / 1000. / 1000.;
        std::stringstream ss;
        ss << "start buffer (base  " << brick_starts_memory << "MB + detail " << detail_starts_memory
           << "MB) + encoding buffers (base " << encoding_memory << "MB + detail " << detail_memory << "MB) = "
           << (brick_starts_memory + encoding_memory + detail_starts_memory + detail_memory) << "MB / " << volume_memory
           << "MB original size (" << (static_cast<double>(brick_starts_memory + encoding_memory + detail_starts_memory + detail_memory)/volume_memory*100.f) << "%) " << str(m_volume_dim) << " voxels."
           << " max. brick palette size " << m_max_brick_palette_count << ".";
        if(m_encodings.size() > 1) {
            ss << "\n        Split encoding buffers (" << m_encodings.size() << "):";
            uint32_t brick_index_count = getBrickCount().x * getBrickCount().y * getBrickCount().z;
            for (int i = 0; i < m_encodings.size(); i++) {
                ss << "\n          " << static_cast<double>(m_encodings[i].size() * sizeof(uint32_t)) / 1000. / 1000. << "MB";
                if(m_separate_detail)
                    ss << " + " << static_cast<double>(m_detail_encodings[i].size() * sizeof(uint32_t)) / 1000. / 1000. << "MB detail";
                ss << ", bricks [" << (m_brick_idx_to_enc_vector * i) << " - " << std::min(m_brick_idx_to_enc_vector * (i+1) - 1, brick_index_count) << "]";
            }
        }
        return ss.str();
    }


    ///////////////////////////////////////////////////////////////////
    ///                 statistics and evaluation                   ///
    ///////////////////////////////////////////////////////////////////
    [[nodiscard]] std::vector<std::map<std::string, float>> gatherBrickStatistics() const;

    void exportAllBrickOperations(const std::string& path) const;
    void exportBrickOperationsToCSV(const std::string& path, uint32_t brick_idx) const;

    static std::vector<glm::uvec4> createBrickPosBuffer(uint32_t brick_size);

    /**
     * Time needed for the full compression pass (without the freq. pre-pass) in seconds.
     */
    float getLastTotalEncodingSeconds() const { return m_last_total_encoding_seconds; }
    /**
     * Time needed for the frequency pre-pass in seconds.
     */
    float getLastTotalFreqPrepassSeconds() const { return m_last_total_freq_prepass_seconds; }

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

    [[nodiscard]] std::vector<uint32_t> getCurrentFrequencyTable() const {
        if(!isUsingRANS())
            throw std::runtime_error("Can't get a frequency table from a Compressed Segmentation Volume that's not using rANS!");
        std::vector<uint32_t> freq(16);
        m_rans.copyCurrentFrequencyTableTo(freq.data());
        return freq;
    }

    [[nodiscard]] const std::vector<uint32_t> getCurrentDetailFrequencyTable() const {
        if(!isUsingDetailFreq())
            throw std::runtime_error("Can't get a detail frequency table from a Compressed Segmentation Volume that's not using rANS in double table mode.");
        std::vector<uint32_t> freq(16);
        m_detail_rans.copyCurrentFrequencyTableTo(freq.data());
        return freq;
    }

    [[nodiscard]] std::string getGLSLSymbolArrayStringRANS() const {
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

    void printBrickInfo(glm::uvec3 brick, loglevel log_level = INFO) const;

    /** A quick way of checking some invariants of CSGV representations to verify the compressed volume.
     * @return true if no errors are found, false otherwise. */
    bool verifyCompression() const;

private:
    uint32_t m_cpu_threads;                         /// number of CPU threads to parallelize computations

    uint32_t m_brick_size;                          /// brick size of each dimension in voxels, must be power of 2
    glm::uvec3 m_volume_dim;                        /// xyz dimensions of the original volume in voxels
    std::vector<std::vector<uint32_t>> m_encodings; /// contains all encodings for all bricks split up by brick id into several vectors
    // ToDo: add user parameter for setting target size in MB per encoding vector
    const uint32_t m_target_uints_per_split_encoding = 536870912u;//20000u; /// targeted max. number of uint32 elements per encoding vector (536870912u -> 2 GB)
    uint32_t m_brick_idx_to_enc_vector = ~0u;       /// dividing 1D brick idx by this value maps to split encoding vector index.
    std::vector<uint32_t> m_brick_starts;           /// points to indices in m_encoding
    std::vector<std::vector<uint32_t>> m_detail_encodings; /// contains the finest LoDs of all bricks if detail separation is enabled
    std::vector<uint32_t> m_detail_starts;          /// points to indices m_detail_encodings

    RANS m_rans;
    RANS m_detail_rans;
    RANSMode m_rANS_mode;
    bool m_parallel_decode = true;                  /// decompresses with in-brick parallelism and encodes bricks accordingly
    bool m_separate_detail;
    uint32_t m_max_brick_palette_count;             /// the max. palette length of any brick as a number of label entries

    // timings [s] of the last compression run (without freq. pre-pass) and the frequency pre-pass
    float m_last_total_encoding_seconds = 0.f;
    float m_last_total_freq_prepass_seconds = 0.f;
    std::string m_label = "";
};

} // namespace vvv
