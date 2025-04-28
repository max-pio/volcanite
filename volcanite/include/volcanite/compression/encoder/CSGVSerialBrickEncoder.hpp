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
    CSGVSerialBrickEncoder(const uint32_t brick_size, const EncodingMode encoding_mode, const uint32_t op_mask = OP_ALL)
        : CSGVBrickEncoder(brick_size, encoding_mode, op_mask), m_rans_initialized(false) {}

    // SERIAL ENCODING -------------------------------------------------------------------------------------------------

    /// Encodes a single brick from given start with size brick_size in the volume to the out vector.
    /// @param volume the labeled voxel volume to encode.
    /// @param out must have enough space reserved for adding all elements.
    /// @param start the start position of the brick. Should be a multiple of the configured brick size.
    /// @param volume_dim the volume size in voxels in each dimension
    /// @return number of uint32_t elements written to out.
    [[nodiscard]] uint32_t encodeBrick(const std::vector<uint32_t> &volume, std::vector<uint32_t> &out,
                                       glm::uvec3 start, glm::uvec3 volume_dim) const override;

    /// Decompresses a single brick.
    /// @param brick_encoding pointer to the contiguous memory region of the brick encoding .
    /// @param brick_encoding_length length of the brick encoding memory region in number of uint32 elements.
    /// @param output_brick is an uint32_t array of the decoded brick. It always has to have brick_size^3 elements.
    /// @param valid_brick_size is used to clamp used voxels for border bricks. Values outside are undefined.
    /// @param inv_lod the LOD until which to decompress, or rather, the decompression iterations. 0 is the coarsest and log2(brick_size) is the original / finest level.
    void decodeBrick(const uint32_t *brick_encoding, uint32_t brick_encoding_length,
                     const uint32_t *brick_detail_encoding, const uint32_t brick_detail_encoding_length,
                     uint32_t *output_brick, glm::uvec3 valid_brick_size, int inv_lod) const override;

    /// Splits the encoding for the brick at brick_encoding into the base encoding including its palette at
    /// base_encoding_out and the encoding of the finest level-of-detail at detail_encoding_out.
    /// @param brick_encoding input brick encoding that is not separated
    /// @param base_encoding_out target to copy the base encoding level to. may overlap with brick_encoding.
    /// @param detail_encoding_out target to copy the detail encoding level to. must not overlap with brick_encoding.
    /// @returns the new base encoding size in numbers of uint32
    uint32_t separateDetail(const std::span<uint32_t> brick_encoding,
                            std::span<uint32_t> base_encoding_out, std::span<uint32_t> detail_encoding_out) const override {

        assert(!m_separate_detail && "encoder already marks detail level as separated");

        // obtain brick information before the content is overwritten:
        const uint32_t header_size = getHeaderSize();
        const uint32_t lod_count = getLodCountPerBrick();
        const uint32_t palette_size = brick_encoding[getPaletteSizeHeaderIndex()];
        // length (in uint32 elements) of the operation stream of base levels only
        const uint32_t base_op_stream_length = brick_encoding[lod_count - 1] / 8 - header_size;
        const uint32_t detail_encoding_size = getDetailLengthBeforeSeparation(brick_encoding.data(), brick_encoding.size());

        // copy the detail encoding to the detail buffer (non-overlapping)
        if (detail_encoding_size > detail_encoding_out.size())
            throw std::runtime_error("detail_encoding_size is too small: " + std::to_string(detail_encoding_size) + " vs. " + std::to_string(detail_encoding_out.size()));

        assert(header_size + base_op_stream_length + detail_encoding_size <= brick_encoding.size() && "detail encoding read overflow in separateDetail");
        assert(detail_encoding_size <= detail_encoding_out.size() && "detail encoding write overflow in separateDetail");
        std::copy_n(brick_encoding.begin() + header_size + base_op_stream_length, detail_encoding_size,
                    detail_encoding_out.begin());
        // memcpy(detail_encoding_out.data(),
        //     brick_encoding.data() + header_size + base_op_stream_length,
        //     detail_encoding_size * sizeof(uint32_t));

        // the header is now missing one element (start pos. of the detail layer): adjust the lod start entries.
        for (int l = 0; l < (lod_count - 1u); l++)
            base_encoding_out[l] = brick_encoding[l] - 8u;

        // move the palette size entry in the encoding header one element to the front
        // (because the encoding_start entry for the detail buffer is now missing in between)
        assert(lod_count == getPaletteSizeHeaderIndex());
        base_encoding_out[getPaletteSizeHeaderIndex() - 1] = brick_encoding[getPaletteSizeHeaderIndex()];
        // move the base encoding (overlapping with brick_encoding)
        assert(header_size + base_op_stream_length <= brick_encoding.size() && "base encoding read overflow in separateDetail");
        memmove(base_encoding_out.data() + (header_size - 1u),
                brick_encoding.data() + header_size,
                base_op_stream_length * sizeof(uint32_t));
        // move the palette (overlapping with brick_encoding)
        assert(header_size + base_op_stream_length + detail_encoding_size + palette_size <= brick_encoding.size() && "palette read overflow in separateDetail");
        assert(header_size - 1u + base_op_stream_length + palette_size <= base_encoding_out.size() && "palette read overflow in separateDetail");
        memmove(base_encoding_out.data() + (header_size - 1u) + base_op_stream_length,
                brick_encoding.data() + header_size + base_op_stream_length + detail_encoding_size,
                palette_size * sizeof(uint32_t));

        // Return new base encoding size, used to update the brick start index:
        // In addition to the detail encoding, brick headers are missing one element (detail LoD start) each.
        return brick_encoding.size() - detail_encoding_size - 1u;
    }

    /// @returns number of uint32_t elements that will be stored for this brick's detail level after detail separation.
    virtual uint32_t getDetailLengthBeforeSeparation(const uint32_t *brick_encoding,
                                                     const uint32_t brick_encoding_length) const override {
        const uint32_t palette_length = brick_encoding[getPaletteSizeHeaderIndex()];
        const uint32_t header_and_base_level_length = brick_encoding[getLodCountPerBrick() - 1u] / 8;
        return brick_encoding_length - header_and_base_level_length - palette_length;
    }

    // VARIABLE BIT-LENGTH ENCODING ------------------------------------------------------------------------------------

    /// Computes operation frequencies and detail operation frequencies (the latter offset by 16) for the brick into the given brick_freq[32] array.
    void freqEncodeBrick(const std::vector<uint32_t> &volume, size_t *brick_freq, glm::uvec3 start,
                         glm::uvec3 volume_dim, bool detail_freq) const override;

    // FILE IMPORT AND EXPORT ------------------------------------------------------------------------------------------

    /// Exports all specialized configuration information of this encoder (e.g. frequency tables) that are not handled
    /// by the encoder base class or CompressedSegmentationVolume class.
    void exportToFile(std::ostream &out) override { CSGVBrickEncoder::exportToFile(out); }

    /// Imports specialized configuration information from the stream.
    /// @return true on success, false otherwise.
    bool importFromFile(std::istream &in) override {
        if (!CSGVBrickEncoder::importFromFile(in))
            return false;
        return true;
    }

    // COMPONENT AND SHADER INTERFACE ----------------------------------------------------------------------------------

    /// returns the index of the uint32_t element in the brick encoding / header that stores the palette size.
    [[nodiscard]] virtual uint32_t getPaletteSizeHeaderIndex() const override { return getHeaderSize() - 1u; }

    /// @returns a list of shader defines used during decoding which are passed to the shader compilation stage
    [[nodiscard]] virtual std::vector<std::string>
    getGLSLDefines(std::function<std::span<const uint32_t>(uint32_t)> getBrickEncodingSpan,
                   uint32_t brick_idx_count) const {
        auto defines = CSGVBrickEncoder::getGLSLDefines(getBrickEncodingSpan, brick_idx_count);
        defines.emplace_back("HEADER_SIZE=" + std::to_string(getHeaderSize()));
        return defines;
    }

    // DEBUGGING AND STATISTICS ----------------------------------------------------------------------------------------

    void verifyBrickCompression(const uint32_t *brick_encoding, uint32_t brick_encoding_length,
                                const uint32_t *detail_encoding, uint32_t detail_encoding_length,
                                std::ostream &error) const override;

    /// Helper method to gather statistics for one single brick. Same as decodeBrick but also:
    /// Unpacks the encoding for the given brick at a given LOD where a value of INVALID is written to octree entries/voxels that are not encoded because a STOP label occurred in a higher level.
    /// The output_palette (if not nullptr) contains the values added by PALETTE_ADV in processed order as uvec4 {label, this_lod, voxel_in_brick_id, 0}
    void decodeBrickWithDebugEncoding(const uint32_t *brick_encoding, const uint32_t brick_encoding_length,
                                      const uint32_t *brick_detail_encoding,
                                      const uint32_t brick_detail_encoding_length,
                                      uint32_t *output_brick, uint32_t *output_encoding,
                                      std::vector<glm::uvec4> *output_palette, glm::uvec3 valid_brick_size,
                                      int inv_lod) const override;

  protected:
    struct ReadState {
        uint32_t idxE = 0u;         // used either as 4 bit element index or byte read index for rANS
        uint32_t rans_state = 0u;   // state of the rANS decoder (not used with NibbleEncoder)
        bool in_detail_lod = false; // if we are in the finest level-of-detail (only set in rANS double table mode)
    };

    // TODO: move rANS attributes to RangeANSEncoder.hpp
    bool m_rans_initialized;
    RANS m_rans;
    RANS m_detail_rans;

    /// Returns the current value in the brick at the neighbor_i neighbor position of brick_pos at the decoding stage at the given lod_width.
    /// If the neighbor is not yet set in this level, the parent element of this neighbor is returned.
    /// If the neighbor would lie outside the brick, UNASSIGNED is returned.
    static uint32_t valueOfNeighbor(const uint32_t *brick, const glm::uvec3 &brick_pos, uint32_t local_lod_i,
                                    uint32_t lod_width, uint32_t brick_size, int neighbor_i);

    static uint32_t valueOfNeighbor(const MultiGridNode *grid, const MultiGridNode *parent_grid,
                                    const glm::uvec3 &brick_pos, uint32_t local_lod_i, uint32_t lod_width,
                                    uint32_t brick_size, int neighbor_i) {
        return CSGVBrickEncoder::valueOfNeighbor(grid, parent_grid, brick_pos,
                                                 local_lod_i, lod_width, brick_size, neighbor_i);
    }

    /// Reads the next element from the brick encoding, possibly using the rANS decoder from this CompressedSegmentationVolume, and updates the state.
    virtual uint32_t readNextLodOperationFromEncoding(const uint32_t *brick_encoding, ReadState &state) const = 0;

    /// returns the size of the header at the beginning of each brick measured in uint32 entries.
    [[nodiscard]] uint32_t getHeaderSize() const { return getLodCountPerBrick() + (m_separate_detail ? 0 : 1); }
};

} // namespace volcanite
