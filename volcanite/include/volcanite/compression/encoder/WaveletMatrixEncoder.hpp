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
    [[nodiscard]] virtual std::vector<std::string> getGLSLDefines(std::function<std::span<const uint32_t>(uint32_t)> getBrickEncodingSpan,
                                                                  uint32_t brick_idx_count) const {
        auto defines = CSGVBrickEncoder::getGLSLDefines(getBrickEncodingSpan, brick_idx_count);
        switch(sizeof(BV_WordType)) {
            case 4:
                defines.emplace_back("BV_WORD_TYPE=uint");
                break;
            case 8:
                defines.emplace_back("BV_WORD_TYPE=uint64_t");
                break;
            default:
                throw std::runtime_error("Missing GLSL define for BV_WORD_TYPE");
        }
        defines.emplace_back("HWM_LEVELS=" + std::to_string(HWM_LEVELS));
        defines.emplace_back("BV_L1_BIT_SIZE=" + std::to_string(BV_L1_BIT_SIZE));
        defines.emplace_back("BV_L2_BIT_SIZE=" + std::to_string(BV_L2_BIT_SIZE));
        defines.emplace_back("BV_L2_WORD_SIZE=" + std::to_string(BV_L2_WORD_SIZE));
        defines.emplace_back("BV_STORE_L1_BITS=" + std::to_string(BV_STORE_L1_BITS));
        defines.emplace_back("BV_STORE_L2_BITS=" + std::to_string(BV_STORE_L2_BITS));
        defines.emplace_back("BV_WORD_BIT_SIZE=" + std::to_string(BV_WORD_BIT_SIZE));
        defines.emplace_back("BASE_HEADER_SIZE=" + std::to_string(getHeaderSize()));
        defines.emplace_back("UINT_PER_L12=" + std::to_string(sizeof(BV_L12Type)/sizeof(uint32_t)));

        // obtain MAX_BIT_VECTOR_WORD_LENGTH as ceil(max_bitvector_bit_length / BV_WORD_BIT_SIZE)
        uint32_t max_bitvector_bit_length = 0u;
        #pragma omp parallel for default(none) shared(brick_idx_count, getBrickEncodingSpan) reduction(max:max_bitvector_bit_length)
        for (uint32_t brick_idx = 0u; brick_idx < brick_idx_count; brick_idx++) {
            auto brick_encoding = getBrickEncodingSpan(brick_idx);
            max_bitvector_bit_length = std::max(max_bitvector_bit_length, getWMHBrickHeaderFromEncoding(brick_encoding.data(), getHeaderSize())->bit_vector_size);
        }
        defines.emplace_back("MAX_BIT_VECTOR_WORD_LENGTH=" + std::to_string((max_bitvector_bit_length + BV_WORD_BIT_SIZE - 1) / BV_WORD_BIT_SIZE));

//        std::stringstream err;
//        verifyBrickCompression(getBrickEncodingSpan(5).data(), getBrickEncodingSpan(5).size(), nullptr, 0, err);
//        Logger(INFO) << "Verify 5: " << err.str() << ".";
//
//        Logger(INFO) << "op-stream 5:  " << outputOperationStream(getBrickEncodingSpan(5), 0, 16);
//        auto wmh = getWMHBrickHeaderFromEncoding(getBrickEncodingSpan(5).data(), getHeaderSize());
//        auto bv = getWMHBitVectorFromEncoding(getBrickEncodingSpan(5).data(), getHeaderSize());
//        std::stringstream bss;
//        for (int i = 0; i < 4; i++)
//            bss << static_cast<uint32_t>(bv[i]) << ", " << static_cast<uint32_t>(bv[i] >> 32) << ", ";
//        Logger(INFO) << "bit-vector start 5:  " << bss.str();
//
//        std::stringstream fss;
//        auto fr = getWMHFlatRankFromEncoding(getBrickEncodingSpan(5).data(), getHeaderSize());
//        uint32_t fr_entries = wmh->bit_vector_size / BV_L1_BIT_SIZE + 1u;
//        for (int i = 0; i < fr_entries; i++) {
//            fss << static_cast<uint32_t>(fr[i]) << ", " << static_cast<uint32_t>(fr[i] >> 32) << ", ";
//        }
//        Logger(INFO) << "flat-rank start 5: " << fss.str();
//
//        Logger(INFO) << "header 5:  bit_vector_size " << wmh->bit_vector_size << ", ones_before_level "
//        << wmh->ones_before_level[0] << ", "<< wmh->ones_before_level[1] << ", "<< wmh->ones_before_level[2] << ", "<< wmh->ones_before_level[3] << ", "
//        << wmh->ones_before_level[4] << ", level_starts_1_to_4 " << wmh->level_starts_1_to_4[0] << ", "  << wmh->level_starts_1_to_4[1]
//        << ", " << wmh->level_starts_1_to_4[2] << ", "  << wmh->level_starts_1_to_4[3] << ", fr[0] " << static_cast<uint32_t>(wmh->fr[0]) << ", " << static_cast<uint32_t>(wmh->fr[0] >> 32);
//
//        std::stringstream fhss;
//        for (int i = 0; i < fr_entries; i++) {
//            fhss << static_cast<uint32_t>(wmh->fr[i]) << ", " << static_cast<uint32_t>(wmh->fr[i] >> 32) << ", ";
//        }
//        Logger(INFO) << "wmh fr start 5: " << fss.str();


        return defines;
    }

    // DEBUGGING AND STATISTICS ----------------------------------------------------------------------------------------

    /// A quick way of checking some invariants of CSGV representations to verify the compressed volume.
    /// Messages must be passed to error if and only if errors are found for this brick.
    virtual void verifyBrickCompression(const uint32_t *brick_encoding, uint32_t brick_encoding_length,
                                        const uint32_t *brick_detail_encoding, uint32_t brick_detail_encoding_length,
                                        std::ostream &error) const {
        // TODO: missing compression verification with wavelet matrix brick encoder

        // Obtain a reference to the uint buffer containing this bricks encoding.
        const uint32_t base_header_size = getHeaderSize();
        const uint32_t total_header_size_one_fr = base_header_size
                                                  + ((m_encoding_mode == WAVELET_MATRIX_ENC) ? sizeof(WMBrickHeader)
                                                                                             : sizeof(WMHBrickHeader)) / 4;
        const uint32_t lod_count = getLodCountPerBrick();
        const uint32_t header_start_lods = lod_count;

        uint32_t total_voxels_in_brick = 0u;
        for (int i = 1; i <= m_brick_size; i <<= 1) {
            total_voxels_in_brick += (i*i*i);
        }

        // check brick having an encoding length greater than header size + 1 operation + 1 palette entry
        if (brick_encoding_length < total_header_size_one_fr + 1u + 1u) {
            error << "brick encoding is shorter than minimum. (header size (incl. 1 flatrank) + 1 encoding + 1 palette) = "
                  << base_header_size + 2u <<  " but is " << brick_encoding_length << "\n";
        }

        // check first header entry being base_header_size * 8
        if(brick_encoding[0] != 0) {
            error << "First encoding operation index must be 0." << "\n";
        }

        // check palette start of first LoD being 0 and second LoD being 1
        if(brick_encoding[header_start_lods] != 0u)
            error << "  first palette start must be 0 but is " << brick_encoding[header_start_lods] << "\n";
        if(brick_encoding[header_start_lods + 1u] != 1u)
            error << "  second palette start must be 1 but is " << brick_encoding[header_start_lods + 1u] << "\n";

        if (m_encoding_mode == WAVELET_MATRIX_ENC) {
            const WMBrickHeader* wm_header = getWMBrickHeaderFromEncoding(brick_encoding, base_header_size);
            if (wm_header->text_size == 0u || wm_header->text_size > total_voxels_in_brick)
                error << "  text size must be within (0, " << total_voxels_in_brick << ") but is " << wm_header->text_size << "\n";
            if (getL1Entry(wm_header->fr[0]) != 0)
                error << "  first flat rank L1 entry must be 0 but is " << getL1Entry(wm_header->fr[0]) << "\n";
            if (getL2Entry(wm_header->fr[0], 0) != 0)
                error << "  first flat rank L1 entry must be 0 but is " << getL1Entry(wm_header->fr[0]) << "\n";
        } else {
            const WMHBrickHeader* wm_header = getWMHBrickHeaderFromEncoding(brick_encoding, base_header_size);
            // maximum text size: HWM_LEVELS bits per voxel (i.e. 5 bit vectors with length of voxels in brick)
            if (wm_header->bit_vector_size == 0u || wm_header->bit_vector_size > total_voxels_in_brick * HWM_LEVELS)
                error << "  bit vector size must be within (0, " << total_voxels_in_brick * HWM_LEVELS << ") but is " << wm_header->bit_vector_size << "\n";
            if (getL1Entry(wm_header->fr[0]) != 0)
                error << "  first flat rank L1 entry must be 0 but is " << getL1Entry(wm_header->fr[0]) << "\n";
            if (getL2Entry(wm_header->fr[0], 0) != 0)
                error << "  first flat rank L1 entry must be 0 but is " << getL1Entry(wm_header->fr[0]) << "\n";
            if (wm_header->ones_before_level[0] != 0u)
                error << "  first ones_before_level entry must be 0 but is " << wm_header->ones_before_level[0] << "\n";
            for (int i = 1; i < 4; i++) {
                if (wm_header->ones_before_level[i] >= wm_header->level_starts_1_to_4[i-1])
                    error << "  ones_before_level[" << i << "] must be < level_starts_1_to_4[" << (i-1) << "] " << wm_header->level_starts_1_to_4[i-1] << " but is " << wm_header->ones_before_level[i] << "\n";
            }
            if (wm_header->level_starts_1_to_4[0] > total_voxels_in_brick)
                error << "  level_starts_1_to_4[0] entry must be the text size, limited by voxel count " << total_voxels_in_brick << " but is " << wm_header->level_starts_1_to_4[0] << "\n";
            for (int i = 0; i < 3; i++) {
                if (wm_header->level_starts_1_to_4[i] > wm_header->level_starts_1_to_4[i+1])
                    error << " level_starts_1_to_4[" << i << "] entry must be <= level_starts_1_to_4[" << (i+1) << "] = " << wm_header->level_starts_1_to_4[i+1] << " but is " << wm_header->level_starts_1_to_4[i] << "\n";
            }
            if (wm_header->level_starts_1_to_4[3] > wm_header->bit_vector_size)
                error << " level_starts_1_to_4[3] entry must be <= bit_vector_size " << wm_header->bit_vector_size << " but is " << wm_header->level_starts_1_to_4[3] << "\n";
        }
    };


    /// @brief returns count many operations starting from offset as a string
    std::string outputOperationStream(const std::span<const uint32_t> encoding, const uint32_t offset, const uint32_t count) const {
        const WMHBrickHeader* wm_header = getWMHBrickHeaderFromEncoding(encoding.data(), getHeaderSize());
        const BV_WordType* bit_vector = getWMHBitVectorFromEncoding(encoding.data(), getHeaderSize());

        std::stringstream ss;
        for (uint32_t i = 0; i < count; i++)
            ss << wm_huffman_access(offset + i, wm_header, bit_vector) << ", ";
        return ss.str();
    }


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
                                               const WMBrickHeader* wm_header,
                                               const BV_WordType* bit_vector);


    static uint32_t decompressCSGVBrickVoxelWMHuffman(const uint32_t output_i, const uint32_t target_inv_lod,
                                                      const glm::uvec3 valid_brick_size,
                                                      const uint32_t *brick_encoding,
                                                      const uint32_t brick_encoding_length,
                                                      const WMHBrickHeader* wm_header,
                                                      const BV_WordType* bit_vector);

    /// returns the size of the header at the beginning of each brick measured in uint32 entries.
    [[nodiscard]] uint32_t getHeaderSize() const { return getLodCountPerBrick() * 2 + 1; }

};

} // namespace volcanite
