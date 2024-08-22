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

#ifndef CSGV_CONSTANTS_H
#define CSGV_CONSTANTS_H

// This header contains compile time CompressedSegmentationVolume constants to be included in the
// CPU encoding/decoding C++ classes as well as in the GLSL shaders for GPU decoding.

// Compressed Segmentation Volume Constants ----------------------------------------------------------------------------

// stream compression mode bit flags
#define DOUBLE_ENCODING_BIT 1u
#define RANS_BIT 2u
#define WT_BIT 4u
#define HUFFMAN_WT_BIT 8u

#ifdef GL_core_profile
    #define CSGV_UINT uint
    #define NO_RANS 0
    #define SINGLE_TABLE_RANS 2
    #define DOUBLE_TABLE_RANS 3
#else
    #define CSGV_UINT uint32_t

namespace volcanite {
        enum EncodingMode {NIBBLE_ENC=0,
                           SINGLE_TABLE_RANS_ENC=1, DOUBLE_TABLE_RANS_ENC=2,
                           WAVELET_MATRIX_ENC=4, HUFFMAN_WM_ENC=8, DOUBLE_TABLE_HUFFMAN_WM_ENC=9};

        // Bit Flags
        //
        // config:
        // double encoder (start new encoder for the finest LOD): bit 0 (1)
        //
        // mutually exclusive:
        // rANS: bit 1 (2)
        // wavelet tree: bit 2 (4)
        // Huffman wavelet tree: bit 3 (8)

        static constexpr const char* EncodingMode_STR(EncodingMode e) {
            switch (e) {
                case NIBBLE_ENC:
                    return "Nibble";
                    break;
                case SINGLE_TABLE_RANS_ENC:
                    return "rANS";
                    break;
                case DOUBLE_TABLE_RANS_ENC:
                    return "rANS-DT";
                    break;
                case WAVELET_MATRIX_ENC:
                    return "WaveletMatrix";
                    break;
                case HUFFMAN_WM_ENC:
                    return "HuffmanWaveletMatrix";
                    break;
                case DOUBLE_TABLE_HUFFMAN_WM_ENC:
                    return "HuffmanWaveletMatrix-DT";
                    break;
                default:
                    return "INVALID_ENC_MODE";
            }
        }


    static constexpr const char* EncodingMode_ShortSTR(EncodingMode e) {
        switch (e) {
            case NIBBLE_ENC:
                return "nb";
                break;
            case SINGLE_TABLE_RANS_ENC:
                return "r1";
                break;
            case DOUBLE_TABLE_RANS_ENC:
                return "r2";
                break;
            case WAVELET_MATRIX_ENC:
                return "wm";
                break;
            case HUFFMAN_WM_ENC:
                return "w1";
                break;
            case DOUBLE_TABLE_HUFFMAN_WM_ENC:
                return "w2";
                break;
            default:
                return "##";
        }
    }
}
#endif

// 1000 most significant bit stores stop bit:
#define STOP_BIT 8u     ///< 1000 MSB bit, stops the hierarchical traversal into finer nodes as they are all constant
// #XXX least significant 3 bits store operation:
#define PARENT 0u       ///< copy label from parent node
// the following must be true: neighbor_z = neighbor_y+1 = neighbor_x+2
#define NEIGHBOR_X 1u   ///< copy label from x-axis neighbor with different parent node or its parent if not decoded
#define NEIGHBOR_Y 2u   ///< copy label from x-axis neighbor with different parent node or its parent if not decoded
#define NEIGHBOR_Z 3u   ///< copy label from x-axis neighbor with different parent node or its parent if not decoded
#define PALETTE_ADV 4u  ///< read palette label at the palette top pointer and advance the top pointer by 1
#define PALETTE_LAST 5u ///< re-read palette last palette label, located 1 entry before the top pointer, again
#define PALETTE_D 6u    ///< re-read palette label from D+2 entries before the top pointer. D follows in encoding stream.

#define INVALID 0xFFFFFFFFu    ///< UINT32_MAX all bits set to 1. Denotes undecoded values, invalid labels, errors..

// Material Constants --------------------------------------------------------------------------------------------------
#define LABEL_AS_ATTRIBUTE 0xFFFFFFFFu

struct GPUSegmentedVolumeMaterial {
    CSGV_UINT discrAttributeStart;  ///< start attribute read location in g_attributes. a value < 0 means to use the label directly (csgv_id)
    float discrIntervalMin;         ///< discrAttribute values within the interval [min, max) assign the label to this material
    float discrIntervalMax;         ///< discrAttribute values within the interval [min, max) assign the label to this material
    CSGV_UINT tfAttributeStart;     ///< start attribute read location in g_attributes
    float tfIntervalMin;            ///< attribute min / max values mapped to the TF interval [0, 1]
    float tfIntervalMax;            ///< attribute min / max values mapped to the TF interval [0, 1]
    float opacity;                  ///< opacity of the material, < 1 is a semi-transparent volume, >= 1 is a surface
    float emission;                 ///< how much radiance the material emits, must be >= 0
    int wrapping;                   ///< wrapping mode to map labels to transfer function values: 0 = clamp, 1 = repeat
};

#endif // CSGV_CONSTANTS_H
