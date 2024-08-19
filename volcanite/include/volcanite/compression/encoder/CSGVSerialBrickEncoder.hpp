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

#include "volcanite/compression/encoder/CSGVBrickEncoder.hpp"
#include "volcanite/compression/pack_rans.hpp"


namespace volcanite {

/// @brief Superclass for the NibbleEncoder and RangeANSEncoder to share common functionality.
class CSGVSerialBrickEncoder : public CSGVBrickEncoder {

public:
    CSGVSerialBrickEncoder(uint32_t brick_size, RANSMode encoding_mode)
            : CSGVBrickEncoder(brick_size, encoding_mode) {}

    // SERIAL ENCODING -------------------------------------------------------------------------------------------------

    /// Encodes a single brick from given start with size brick_size in the volume to the out vector.
    /// @param volume the labeled voxel volume to encode.
    /// @param out must have enough space reserved for adding all elements.
    /// @param start the start position of the brick. Should be a multiple of the configured brick size.
    /// @param volume_dim the volume size in voxels in each dimension
    /// @return number of uint32_t elements written to out.
    [[nodiscard]] uint32_t encodeBrick(const std::vector<uint32_t>& volume, std::vector<uint32_t>& out,
                                               glm::uvec3 start, glm::uvec3 volume_dim) const override;

    /// Decompresses a single brick.
    /// @param brick_encoding pointer to the contiguous memory region of the brick encoding .
    /// @param brick_encoding_length length of the brick encoding memory region in number of uint32 elements.
    /// @param output_brick is an uint32_t array of the decoded brick. It always has to have brick_size^3 elements.
    /// @param valid_brick_size is used to clamp used voxels for border bricks. Values outside are undefined.
    /// @param inv_lod the LOD until which to decompress, or rather, the decompression iterations. 0 is the coarsest and log2(brick_size) is the original / finest level.
    virtual void decodeBrick(const uint32_t* brick_encoding, const uint32_t brick_encoding_length,
                             const uint32_t* brick_detail_encoding, const uint32_t brick_detail_encoding_length,
                             uint32_t* output_brick, glm::uvec3 valid_brick_size, int inv_lod) const override;

    /// Splits the encoding for the brick at brick_encoding into the base encoding including its palette at
    /// base_encoding_out and the encoding of the finest level-of-detail at detail_encoding_out.
    virtual void separateDetail(const uint32_t* brick_encoding, uint32_t* base_encoding_out,
                                uint32_t* detail_encoding_out) const override {
        throw std::runtime_error("Brick encoding detail separation not yet implemented.");
    }

    // VARIABLE BIT-LENGTH ENCODING ------------------------------------------------------------------------------------

    /// Computes operation frequencies and detail operation frequencies (the latter offset by 16) for the brick into the given brick_freq[32] array.
    virtual void freqEncodeBrick(const std::vector<uint32_t>& volume, size_t* brick_freq, glm::uvec3 start,
                                 glm::uvec3 volume_dim, bool detail_freq) const override;

    // COMPONENT AND SHADER INTERFACE ----------------------------------------------------------------------------------

    /// returns the index of the uint32_t element in the brick encoding / header that stores the palette size.
    [[nodiscard]] virtual uint32_t getPaletteSizeHeaderIndex() const override { return getHeaderSize() - 1u; }

    // DEBUGGING AND STATISTICS ----------------------------------------------------------------------------------------

    void verifyBrickCompression(const uint32_t* brick_encoding, uint32_t brick_encoding_length,
                                const uint32_t* detail_encoding, uint32_t detail_encoding_length,
                                std::stringstream &error) const override;

    /// Helper method to gather statistics for one single brick. Same as decodeBrick but also:
    /// Unpacks the encoding for the given brick at a given LOD where a value of INVALID is written to octree entries/voxels that are not encoded because a STOP label occurred in a higher level.
    /// The output_palette (if not nullptr) contains the values added by PALETTE_ADV in processed order as uvec4 {label, this_lod, voxel_in_brick_id, 0}
    void decodeBrickWithDebugEncoding(const uint32_t* brick_encoding, const uint32_t brick_encoding_length,
                                      const uint32_t* brick_detail_encoding,
                                      const uint32_t brick_detail_encoding_length,
                                      uint32_t* output_brick, uint32_t* output_encoding,
                                      std::vector<glm::uvec4>* output_palette, glm::uvec3 valid_brick_size,
                                      int inv_lod) const override;

protected:
    struct ReadState {
        uint32_t idxE = 0u;             // used either as 4 bit element index or byte read index for rANS
        uint32_t rans_state = 0u;       // state of the rANS decoder
        bool in_detail_lod = false;     // if we are in the finest level-of-detail (only set in rANS double table mode)
    };

    RANS m_rans;
    RANS m_detail_rans;

    /// Returns the current value in the brick at the neighbor_i neighbor position of brick_pos at the decoding stage at the given lod_width.
    /// If the neighbor is not yet set in this level, the parent element of this neighbor is returned.
    /// If the neighbor would lie outside the brick, UNASSIGNED is returned.
    static uint32_t valueOfNeighbor(const uint32_t* brick, const glm::uvec3& brick_pos, uint32_t local_lod_i,
                                    uint32_t lod_width, uint32_t brick_size, int neighbor_i);

    static uint32_t valueOfNeighbor(const MultiGridNode* grid, const MultiGridNode* parent_grid,
                                    const glm::uvec3& brick_pos, uint32_t local_lod_i, uint32_t lod_width,
                                    uint32_t brick_size, int neighbor_i) {
        return CSGVBrickEncoder::valueOfNeighbor(grid, parent_grid, brick_pos,
                                                 local_lod_i, lod_width, brick_size, neighbor_i);
    }

    /// Reads the next element from the brick encoding, possibly using the rANS decoder from this CompressedSegmentationVolume, and updates the state.
    virtual uint32_t readNextLodOperationFromEncoding(const uint32_t* brick_encoding, ReadState& state) const = 0;

    /// returns the size of the header at the beginning of each brick measured in uint32 entries.
    [[nodiscard]] uint32_t getHeaderSize() const { return getLodCountPerBrick() * 2 + (m_separate_detail ? 0 : 1); }
};

} // namespace volcanite
