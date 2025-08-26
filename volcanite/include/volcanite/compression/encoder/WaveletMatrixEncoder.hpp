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
#include "volcanite/compression/wavelet_tree/HuffmanWaveletMatrix.hpp"

namespace volcanite {

class WaveletMatrixEncoder : public CSGVBrickEncoder {

  public:
    WaveletMatrixEncoder(uint32_t brick_size, EncodingMode encoding_mode, uint32_t op_mask = OP_ALL)
        : CSGVBrickEncoder(brick_size, encoding_mode, op_mask) {
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
    [[nodiscard]] uint32_t
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
    [[nodiscard]] virtual uint32_t getPaletteSizeHeaderIndex() const { return getLodCountPerBrick(); }

    /// @returns a list of shader defines used during decoding which are passed to the shader compilation stage
    [[nodiscard]] virtual std::vector<std::string>
    getGLSLDefines(std::function<std::span<const uint32_t>(uint32_t)> getBrickEncodingSpan,
                   uint32_t brick_idx_count) const;

    // FILE IMPORT AND EXPORT ------------------------------------------------------------------------------------------

    uint32_t getCompileConstantsHash() {
        const std::vector<uint32_t> keys = {sizeof(BV_WordType), sizeof(BV_L12Type), HWM_LEVELS, BV_L1_BIT_SIZE,
                                            BV_L2_BIT_SIZE, BV_L2_WORD_SIZE, BV_STORE_L1_BITS, BV_STORE_L2_BITS,
                                            BV_WORD_BIT_SIZE, getWMHeaderIndex()};
        uint32_t hash = 0u;
        for (const auto &k : keys)
            hash = (std::hash<unsigned char>{}(k ^ (std::rotl<size_t>(hash, 1))));
        return hash;
    }

    /// Exports all specialized configuration information of this encoder (e.g. frequency tables) that are not handled
    /// by the encoder base class or CompressedSegmentationVolume class.
    void exportToFile(std::ostream &out) override {
        CSGVBrickEncoder::exportToFile(out);
        uint32_t compile_constant_hash = getCompileConstantsHash();
        out.write(reinterpret_cast<char *>(&compile_constant_hash), sizeof(uint32_t));
    }

    /// Imports specialized configuration information from the stream.
    /// @return true on success, false otherwise.
    bool importFromFile(std::istream &in) override {
        if (!CSGVBrickEncoder::importFromFile(in))
            return false;

        uint32_t compile_constant_hash;
        in.read(reinterpret_cast<char *>(&compile_constant_hash), sizeof(uint32_t));
        if (compile_constant_hash != getCompileConstantsHash()) {
            Logger(Error) << "WaveletMatrixEncoder import error: file was encoded with different compile constants.";
            return false;
        }
        return true;
    }

    // DEBUGGING AND STATISTICS ----------------------------------------------------------------------------------------

    /// A quick way of checking some invariants of CSGV representations to verify the compressed volume.
    /// Messages must be passed to error if and only if errors are found for this brick.
    void verifyBrickCompression(const uint32_t *brick_encoding, uint32_t brick_encoding_length,
                                const uint32_t *brick_detail_encoding, uint32_t brick_detail_encoding_length,
                                std::ostream &error) const override;

    /// @brief returns count many operations starting from offset as a string
    [[nodiscard]] std::string
    outputOperationStream(std::span<const uint32_t> encoding, uint32_t offset, uint32_t count) const;

    void getBrickStatistics(std::map<std::string, float> &statistics,
                            const uint32_t *brick_encoding, const uint32_t brick_encoding_length,
                            glm::uvec3 valid_brick_size) const override;

  private:
    static uint32_t decompressCSGVBrickVoxelWM(uint32_t output_i, uint32_t target_inv_lod,
                                               glm::uvec3 valid_brick_size,
                                               const uint32_t *brick_encoding,
                                               uint32_t brick_encoding_length,
                                               const WMBrickHeader *wm_header,
                                               const BV_WordType *bit_vector);

    static uint32_t decompressCSGVBrickVoxelWMHuffman(uint32_t output_i, uint32_t target_inv_lod,
                                                      glm::uvec3 valid_brick_size,
                                                      const uint32_t *brick_encoding,
                                                      uint32_t brick_encoding_length,
                                                      const WMHBrickHeader *wm_header,
                                                      const BV_WordType *bit_vector,
                                                      const FlatRank_BitVector_ptrs &stop_bits);

    // ===============================================================================================================//
    //                                         ENCODING COMPONENT ACCESS                                              //
    // ===============================================================================================================//

    // common -----------

    // @return the uint32 offset in the brick encoding where the wavelet matrix or Huffman wavelet matrix brick header
    // is stored.
    [[nodiscard]] uint32_t getWMHeaderIndex() const {
        if (m_encoding_mode == WAVELET_MATRIX_ENC) {
            // the non-Huffman Wavelet Matrix header struct contains the palette size to ensure a correct padding
            return getLodCountPerBrick();
        } else if (m_encoding_mode == HUFFMAN_WM_ENC) {
            return getLodCountPerBrick() + 1u;
        } else {
            throw std::runtime_error("encoding mode not supported by wavelet matrix encoder");
        }
    }

    // Wavelet Matrix -------
    inline const WMBrickHeader *getWMBrickHeaderFromEncoding(const uint32_t *v) const;
    inline const BV_WordType *getWMBitVectorFromEncoding(const uint32_t *v) const;

    // Huffman Wavelet Matrix -------
    inline const WMHBrickHeader *getWMHBrickHeaderFromEncoding(const uint32_t *v) const;
    inline const BV_L12Type *getWMHFlatRankFromEncoding(const uint32_t *v) const;
    inline const BV_WordType *getWMHBitVectorFromEncoding(const uint32_t *v) const;
    static FlatRank_BitVector_ptrs getWMHStopBitsFromEncoding(const uint32_t *brick_encoding,
                                                              uint32_t brick_encoding_length, uint32_t palette_size);
};

} // namespace volcanite
