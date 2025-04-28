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

#include "csgv_constants.incl"
#include <bit>
#include <functional>
#include <glm/glm.hpp>
#include <map>
#include <span>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace volcanite {

struct MultiGridNode;

/// @brief The brick encoder handle encoding and decoding of the segmentation volume within a single brick.
/// This is an abstract class from which different encoders are implemented. While the abstract interface specifies
/// methods for serial, variable bit length, and random access encoding, a given subclass may not implement all of them.
/// The brick size is a template for the encoder to allow compile time optimizations. The encoder stores no state but
/// only its general configuration instead. It does not check if it decodes a brick from a memory region in the same way
/// it was encoded.\n\n
/// The following invariants must be true for all encoders:\n
/// * the uint32 label palette must be stored at the end of the (base) encoding,\n
/// * one uint32 at position getPlatteHeaderSizeIndex() must store the size of the palette.
class CSGVBrickEncoder {

  public:
    CSGVBrickEncoder() = delete;
    explicit CSGVBrickEncoder(uint32_t brick_size, EncodingMode encoding_mode, uint32_t op_mask = OP_ALL)
        : m_brick_size(brick_size), m_encoding_mode(encoding_mode), m_op_mask(op_mask),
          m_separate_detail(false), m_cpu_threads(std::thread::hardware_concurrency()) {
        assert(std::popcount(brick_size) == 1u && "Encoding brick size must be a positive power of two.");
    }

    virtual ~CSGVBrickEncoder() = default;

    // SERIAL ENCODING -------------------------------------------------------------------------------------------------

    /// Encodes a single brick from given start with size brick_size in the volume to the out vector.
    /// @param volume the labeled voxel volume to encode.
    /// @param out must have enough space reserved for adding all elements.
    /// @param start the start position of the brick. Should be a multiple of the configured brick size.
    /// @param volume_dim the volume size in voxels in each dimension
    /// @return number of uint32_t elements written to out.
    [[nodiscard]] virtual uint32_t encodeBrick(const std::vector<uint32_t> &volume, std::vector<uint32_t> &out,
                                               glm::uvec3 start, glm::uvec3 volume_dim) const = 0;

    /// Decompresses a single brick.
    /// @param brick_encoding pointer to the contiguous memory region of the brick encoding .
    /// @param brick_encoding_length length of the brick encoding memory region in number of uint32 elements.
    /// @param output_brick is an uint32_t array of the decoded brick. It always has to have brick_size^3 elements.
    /// @param valid_brick_size is used to clamp used voxels for border bricks. Values outside are undefined.
    /// @param inv_lod the LOD until which to decompress, or rather, the decompression iterations. 0 is the coarsest and log2(brick_size) is the original / finest level.
    virtual void decodeBrick(const uint32_t *brick_encoding, uint32_t brick_encoding_length,
                             const uint32_t *brick_detail_encoding, const uint32_t brick_detail_encoding_length,
                             uint32_t *output_brick, glm::uvec3 valid_brick_size, int inv_lod) const = 0;

    /// Splits the encoding for the brick at brick_encoding into the base encoding including its palette at
    /// base_encoding_out and the encoding of the finest level-of-detail at detail_encoding_out.
    /// @param brick_encoding input brick encoding that is not separated
    /// @param base_encoding_out target to copy the base encoding level to. may overlap with brick_encoding.
    /// @param detail_encoding_out target to copy the detail encoding level to. must not overlap with brick_encoding.
    /// @returns the new base encoding size in numbers of uint32
    virtual uint32_t separateDetail(const std::span<uint32_t> brick_encoding,
                                    std::span<glm::uint32> base_encoding_out,
                                    std::span<glm::uint32> detail_encoding_out) const {
        throw std::logic_error("CSGV brick encoder does not implement detail separation.");
    }

    /// @returns number of uint32_t elements that will be stored for this brick's detail level after detail separation.
    virtual uint32_t getDetailLengthBeforeSeparation(const uint32_t *brick_encoding, const uint32_t brick_encoding_length) const {
        throw std::logic_error("CSGV brick encoder does not implement detail separation.");
    }

    // RANDOM ACCESS DECODING ------------------------------------------------------------------------------------------

    /// Encodes a single brick from given start with size brick_size in the volume to the out vector for in-brick random
    /// access. This allows in-brick parallel decoding.
    /// @param volume the labeled voxel volume to encode.
    /// @param out must have enough space reserved for adding all elements.
    /// @param start the start position of the brick. Should be a multiple of the configured brick size.
    /// @param volume_dim the volume size in voxels in each dimension
    /// @return number of uint32_t elements written to out
    [[nodiscard]] virtual uint32_t encodeBrickForRandomAccess(const std::vector<uint32_t> &volume, std::vector<uint32_t> &out, glm::uvec3 start, glm::uvec3 volume_dim) const {
        throw std::logic_error("CSGV brick encoder does not implement random access encoding.");
    }

    /// Decodes a single voxel from the brick encoding. Requires random_access to be enabled for random access
    /// within a brick. Must be used with a plain 4 bit encoding.
    /// @param output_i the voxel's brick encoding index within the target inverse lod
    /// @param target_inv_lod the target inverse level-of-detail of the voxel to decode
    /// @param brick_encoding uint32 pointer to the start of the brick encoding
    /// @param brick_encoding_length the length in uint32 elements of the brick encoding
    /// @returns the label of the brick voxel corresponding to the brick encoding index output_i
    virtual uint32_t decompressCSGVBrickVoxel(const uint32_t output_i, const uint32_t target_inv_lod,
                                              const glm::uvec3 valid_brick_size,
                                              const uint32_t *brick_encoding, const uint32_t brick_encoding_length) const {
        throw std::logic_error("CSGV brick encoder does not implement random access encoding.");
    }

    /// Decompresses a single brick in parallel.
    /// @param brick_encoding memory location of the brick encoding.
    /// @param brick_encoding_length length of the complete brick encoding in number of uint32 elements.
    /// @param output_brick is an uint32_t array of the decoded brick. It always has to have brick_size^3 elements.
    /// @param valid_brick_size is used to clamp used voxels for border bricks. Values outside are undefined.
    /// @param target_inv_lod the LOD until which to decompress. 0 is the coarsest and log2(brick_size) is the original / finest level.
    virtual void parallelDecodeBrick(const uint32_t *brick_encoding, const uint32_t brick_encoding_length,
                                     uint32_t *output_brick, glm::uvec3 valid_brick_size, int target_inv_lod) const {
        throw std::logic_error("CSGV brick encoder does not implement random access encoding.");
    }

    // VARIABLE BIT-LENGTH ENCODING ------------------------------------------------------------------------------------

    // TODO: the CSGV encoders should not expose frequency tables but handle them inside their object only

    /// Computes operation frequencies and detail operation frequencies (the latter offset by 16) for the brick into the given brick_freq[32] array.
    virtual void freqEncodeBrick(const std::vector<uint32_t> &volume, size_t *brick_freq, glm::uvec3 start,
                                 glm::uvec3 volume_dim, bool detail_freq) const {
        throw std::logic_error("CSGV brick encoder does not implement variable bit length encoding.");
    };

    /// Computes operation frequencies and detail operation frequencies (the latter offset by 16) for the brick into the given brick_freq[32] array.
    virtual void freqEncodeBrickForRandomAccess(const std::vector<uint32_t> &volume, size_t *brick_freq,
                                                glm::uvec3 start, glm::uvec3 volume_dim, bool detail_freq) const {
        throw std::logic_error("CSGV brick encoder does not implement random access encoding.");
    }

    // COMPONENT AND SHADER INTERFACE ----------------------------------------------------------------------------------

    /// @returns the index of the uint32_t element in the brick encoding / header that stores the palette size.
    [[nodiscard]] virtual uint32_t getPaletteSizeHeaderIndex() const = 0;

    /// @returns a list of shader defines used during decoding which are passed to the shader compilation stage
    [[nodiscard]] virtual std::vector<std::string> getGLSLDefines(std::function<std::span<const uint32_t>(uint32_t)> getBrickEncodingSpan,
                                                                  uint32_t brick_idx_count) const {
        std::vector<std::string> defines{"ENCODING_MODE=" + std::to_string(m_encoding_mode),
                                         "BRICK_SIZE=" + std::to_string(m_brick_size),
                                         "LOD_COUNT=" + std::to_string(getLodCountPerBrick()),
                                         "PALETTE_SIZE_HEADER_INDEX=" + std::to_string(getPaletteSizeHeaderIndex()),
                                         "OP_MASK=" + std::to_string(m_op_mask)};
        return defines;
    }

    // FILE IMPORT AND EXPORT ------------------------------------------------------------------------------------------

    /// Exports all specialized configuration information of this encoder (e.g. frequency tables) that are not handled
    /// by the encoder base class or CompressedSegmentationVolume class.
    virtual void exportToFile(std::ostream &out) {}

    /// Imports specialized configuration information from the stream.
    /// @return true on success, false otherwise.
    virtual bool importFromFile(std::istream &in) { return true; }

    // DEBUGGING AND STATISTICS ----------------------------------------------------------------------------------------

    /// A quick way of checking some invariants of CSGV representations to verify the compressed volume.
    /// Messages must be passed to error if and only if errors are found for this brick.
    virtual void verifyBrickCompression(const uint32_t *brick_encoding, uint32_t brick_encoding_length,
                                        const uint32_t *brick_detail_encoding, uint32_t brick_detail_encoding_length,
                                        std::ostream &error) const = 0;

    /// A quick way of checking some invariants of CSGV representations to verify the compressed volume.
    /// @returns true if the brick is valid, false otherwise
    virtual bool verifyBrickCompression(const uint32_t *brick_encoding, uint32_t brick_encoding_length,
                                        const uint32_t *brick_detail_encoding,
                                        uint32_t brick_detail_encoding_length) const {
        std::stringstream ss;
        verifyBrickCompression(brick_encoding, brick_encoding_length,
                               brick_detail_encoding, brick_detail_encoding_length, ss);
        return ss.str().empty();
    };

    /// Helper method to gather statistics for one single brick. Same as decodeBrick but also:
    /// Unpacks the encoding for the given brick at a given LOD where a value of INVALID is written to octree entries/voxels that are not encoded because a STOP label occurred in a higher level.
    /// The output_palette (if not nullptr) contains the values added by PALETTE_ADV in processed order as uvec4 {label, this_lod, voxel_in_brick_id, 0}
    virtual void decodeBrickWithDebugEncoding(const uint32_t *brick_encoding, const uint32_t brick_encoding_length,
                                              const uint32_t *brick_detail_encoding,
                                              const uint32_t brick_detail_encoding_length,
                                              uint32_t *output_brick, uint32_t *output_encoding,
                                              std::vector<glm::uvec4> *output_palette, glm::uvec3 valid_brick_size,
                                              int inv_lod) const {
        throw std::logic_error("CSGV brick encoder does not implement debugging decoding.");
    }

    virtual void getBrickStatistics(std::map<std::string, float> &statistics,
                                    const uint32_t *brick_encoding, const uint32_t brick_encoding_length,
                                    glm::uvec3 valid_brick_size) const {}

    // CONFIGURATION ---------------------------------------------------------------------------------------------------
    void setCPUThreadCount(uint32_t thread_count = 0u) {
        m_cpu_threads = (thread_count == 0u) ? std::thread::hardware_concurrency() : thread_count;
    }

    /// If set to true, the decoding stages assume separated detail buffers. separateDetail() must be have been applied
    /// on any previously encoded brick before further decoding. Otherwise, the decoder will produce false results.
    virtual void setDecodeWithSeparateDetail(bool decode_with_separate_detail) {
        m_separate_detail = decode_with_separate_detail;
    }

  protected:
    // configuration
    uint32_t m_brick_size;
    EncodingMode m_encoding_mode;
    uint32_t m_op_mask; ///< mask for enabling / disabling certain CSGV operations
    bool m_separate_detail;
    uint32_t m_cpu_threads;

    static constexpr uint32_t MAX_PALETTE_DELTA_DISTANCE = 1u << 24u; // (practically unlimited) longer distances would require more bits than a palette entry

  protected:
    /// list of neighbor vectors per index (8 indices in total) where each index has 3 ivec3 vectors of type {-1, 1}^3
    static constexpr glm::ivec3 neighbor[8][3] = {{glm::ivec3({-1, 0, 0}), glm::ivec3({0, -1, 0}), glm::ivec3({0, 0, -1})},
                                                  {glm::ivec3({1, 0, 0}), glm::ivec3({0, -1, 0}), glm::ivec3({0, 0, -1})},
                                                  {glm::ivec3({-1, 0, 0}), glm::ivec3({0, 1, 0}), glm::ivec3({0, 0, -1})},
                                                  {glm::ivec3({1, 0, 0}), glm::ivec3({0, 1, 0}), glm::ivec3({0, 0, -1})},
                                                  {glm::ivec3({-1, 0, 0}), glm::ivec3({0, -1, 0}), glm::ivec3({0, 0, 1})},
                                                  {glm::ivec3({1, 0, 0}), glm::ivec3({0, -1, 0}), glm::ivec3({0, 0, 1})},
                                                  {glm::ivec3({-1, 0, 0}), glm::ivec3({0, 1, 0}), glm::ivec3({0, 0, 1})},
                                                  {glm::ivec3({1, 0, 0}), glm::ivec3({0, 1, 0}), glm::ivec3({0, 0, 1})}};

    static uint32_t valueOfNeighbor(const MultiGridNode *grid, const MultiGridNode *parent_grid,
                                    const glm::uvec3 &brick_pos, uint32_t local_lod_i, uint32_t lod_width,
                                    uint32_t brick_size, int neighbor_i);

    /// @returns the number of levels-of-detail that each brick with the given brick size B has as log2(B) + 1
    [[nodiscard]] uint32_t getLodCountPerBrick() const { return glm::findMSB(m_brick_size) + 1; }
};

} // namespace volcanite
