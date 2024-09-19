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
#include "volcanite/compression/pack_wavelet_matrix.hpp"

namespace volcanite {

class WaveletMatrixEncoder : public CSGVBrickEncoder {

public:
    WaveletMatrixEncoder(uint32_t brick_size, EncodingMode encoding_mode)
            : CSGVBrickEncoder(brick_size, encoding_mode) {
        if (encoding_mode != WAVELET_MATRIX_ENC && encoding_mode != HUFFMAN_WM_ENC)
            throw std::runtime_error("WaveletMatrixEncoder must be used with (Huffman) WAVELET_MATRIX encoding mode.");
    }

    void setDecodeWithSeparateDetail(bool decode_with_separate_detail) override {
        if (decode_with_separate_detail)
            throw std::logic_error("WaveletMatrixEncoder does not support detail separation.");
    }

    // SERIAL ENCODING -------------------------------------------------------------------------------------------------

    /// Encodes a single brick from given start with size brick_size in the volume to the out vector.
    /// @param volume the labeled voxel volume to encode.
    /// @param out must have enough space reserved for adding all elements.
    /// @param start the start position of the brick. Should be a multiple of the configured brick size.
    /// @param volume_dim the volume size in voxels in each dimension
    /// @return number of uint32_t elements written to out.
    [[nodiscard]] virtual uint32_t
    encodeBrick(const std::vector<uint32_t> &volume, std::vector<uint32_t> &out, glm::uvec3 start,
                glm::uvec3 volume_dim) const override {
        return encodeBrickForRandomAccess(volume, out, start, volume_dim);
    }

    /// Decompresses a single brick.
    /// @param brick_encoding pointer to the contiguous memory region of the brick encoding .
    /// @param brick_encoding_length length of the brick encoding memory region in number of uint32 elements.
    /// @param output_brick is an uint32_t array of the decoded brick. It always has to have brick_size^3 elements.
    /// @param valid_brick_size is used to clamp used voxels for border bricks. Values outside are undefined.
    /// @param inv_lod the LOD until which to decompress, or rather, the decompression iterations. 0 is the coarsest
    ///                and log2(brick_size) is the original / finest level.
    virtual void decodeBrick(const uint32_t *brick_encoding, const uint32_t brick_encoding_length,
                             const uint32_t *brick_detail_encoding, const uint32_t brick_detail_encoding_length,
                             uint32_t *output_brick, glm::uvec3 valid_brick_size, int inv_lod) const override {
        throw std::runtime_error("Serial decoding of wavelet matrix encoded bricks is not yet implemented.");
    }

    // RANDOM ACCESS DECODING ------------------------------------------------------------------------------------------

    /// Encodes a single brick from given start with size brick_size in the volume to the out vector for in-brick random
    /// access. This allows in-brick parallel decoding.
    /// @param volume the labeled voxel volume to encode.
    /// @param out must have enough space reserved for adding all elements.
    /// @param start the start position of the brick. Should be a multiple of the configured brick size.
    /// @param volume_dim the volume size in voxels in each dimension
    /// @return number of uint32_t elements written to out
    [[nodiscard]] virtual uint32_t encodeBrickForRandomAccess(const std::vector<uint32_t> &volume,
                                                              std::vector<uint32_t> &out, glm::uvec3 start,
                                                              glm::uvec3 volume_dim) const override;

    /// Decodes a single voxel from the brick encoding. Requires random_access to be enabled for random access
    /// within a brick. Must be used with a plain 4 bit encoding.
    /// @param output_i the voxel's brick encoding index within the target inverse lod
    /// @param target_inv_lod the target inverse level-of-detail of the voxel to decode
    /// @param brick_encoding uint32 pointer to the start of the brick encoding
    /// @param brick_encoding_length the length in uint32 elements of the brick encoding
    /// @returns the label of the brick voxel corresponding to the brick encoding index output_i
    virtual uint32_t decompressCSGVBrickVoxel(const uint32_t output_i, const uint32_t target_inv_lod,
                                              const glm::uvec3 valid_brick_size, const uint32_t *brick_encoding,
                                              const uint32_t brick_encoding_length) const override;

    /// Decompresses a single brick in parallel.
    /// @param brick_encoding pointer to the contiguous memory region of the brick encoding .
    /// @param brick_encoding_length length of the brick encoding memory region in number of uint32 elements.
    /// @param output_brick is an uint32_t array of the decoded brick. It always has to have brick_size^3 elements.
    /// @param valid_brick_size is used to clamp used voxels for border bricks. Values outside are undefined.
    /// @param target_inv_lod the LOD until which to decompress. 0 is the coarsest and log2(brick_size) is the original / finest level.
    virtual void parallelDecodeBrick(const uint32_t *brick_encoding, const uint32_t brick_encoding_length,
                                     uint32_t *output_brick, glm::uvec3 valid_brick_size,
                                     int target_inv_lod) const override;

    // VARIABLE BIT-LENGTH ENCODING ------------------------------------------------------------------------------------

    void freqEncodeBrickForRandomAccess(const std::vector<uint32_t> &volume, size_t *brick_freq,
                                        glm::uvec3 start, glm::uvec3 volume_dim, bool detail_freq) const override {
        throw std::runtime_error("freq encoding for random access not yet implemented");
    }


    // COMPONENT AND SHADER INTERFACE ----------------------------------------------------------------------------------

    /// @returns the index of the uint32_t element in the brick encoding / header that stores the palette size.
    [[nodiscard]] virtual uint32_t getPaletteSizeHeaderIndex() const { return getHeaderSize() - 1u; }

    /// @returns a list of shader defines used during decoding which are passed to the shader compilation stage
    [[nodiscard]] virtual std::vector<std::string> getGLSLDefines() const { return CSGVBrickEncoder::getGLSLDefines(); }

    // DEBUGGING AND STATISTICS ----------------------------------------------------------------------------------------

    /// A quick way of checking some invariants of CSGV representations to verify the compressed volume.
    /// Messages must be passed to error if and only if errors are found for this brick.
    virtual void verifyBrickCompression(const uint32_t *brick_encoding, uint32_t brick_encoding_length,
                                        const uint32_t *brick_detail_encoding, uint32_t brick_detail_encoding_length,
                                        std::ostream &error) const {
        // TODO: missing compression verification with wavelet matrix brick encoder
    };


private:
    /// Returns the number of operations stored in a brick (one per output voxel) when no stop bits are used.
    inline uint32_t getMaxOperationsInBrick() const {
        return getMaxOperationsUpToInvLoD(getLodCountPerBrick() - 1u);
    }

    /// Returns the number of operations in a brick (one per output voxel) when no stop bits are used up to inv. LoD
    inline uint32_t getMaxOperationsUpToInvLoD(uint32_t inv_lod) const {
        // ignoring stop bits:
        // a brick contains 1 operation for the coarsest LoD, 2*2*2=8 for the next LoD, 4*4*4=64 for the next loD, ...
        // For the first N inverse LoDs this results in a total number of operations of
        //     SUM_0^N (2^n)^3  = 1/7 (8^(i+1) - 1)
        return ((1u << 3u * (inv_lod + 1u)) - 1u) / 7u;
    }

    static uint32_t decompressCSGVBrickVoxelWM(const uint32_t output_i, const uint32_t target_inv_lod,
                                               const glm::uvec3 valid_brick_size,
                                               const uint32_t *brick_encoding,
                                               const uint32_t brick_encoding_length,
                                               const WMBrickHeader &wm_header);


    static uint32_t decompressCSGVBrickVoxelWMHuffman(const uint32_t output_i, const uint32_t target_inv_lod,
                                                      const glm::uvec3 valid_brick_size,
                                                      const uint32_t *brick_encoding,
                                                      const uint32_t brick_encoding_length,
                                                      const WMHBrickHeader &wm_header);

    /// returns the size of the header at the beginning of each brick measured in uint32 entries.
    [[nodiscard]] uint32_t getHeaderSize() const { return getLodCountPerBrick() * 2 + 1; }

};

} // namespace volcanite
