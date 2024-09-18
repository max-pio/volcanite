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

#ifndef ENCODER_SERIAL_GLSL
#define ENCODER_SERIAL_GLSL

#include "volcanite/compression/csgv_utils.glsl"

bool verifyBrickCompressionShared(const uint brick_start, const uint brick_encoding_length) {

    const uint header_size = g_lod_count * 2 + 1u;
    const uint header_start_lods = g_lod_count;

    // check brick having an encoding length greater than header size + 1 operation + 1 palette entry
    if (brick_encoding_length < header_size + 1u + 1u) {
        debugPrintfEXT("brick encoding is shorter than minimum. (header size + 1 encoding + 1 palette) = %u but is %u", header_size + 2u, brick_encoding_length);
        return false;
    }

    // check first header entry being header_size * 8
    if(CSGV_SHARED_MEMORY_BRICK_ENCODING[brick_start + 0] != header_size * 8u) {
        debugPrintfEXT("first encoding starts 4bit must be header*8");
        return false;
    }

    // check encoding starts being in ascending order
    // note: the header count the number of entries, except the last entry when using double table rANS
    // for which this entry refers to the raw 4 bit index at which the detail encoding starts AFTER packing the earlier LoDs
    for (uint l = 1; l < header_start_lods; l++) {
        uint distance = CSGV_SHARED_MEMORY_BRICK_ENCODING[brick_start + l] - CSGV_SHARED_MEMORY_BRICK_ENCODING[brick_start + l - 1];
        if(distance < 0u) {
            debugPrintfEXT("encoding starts are not in ascending order");
            return false;
        }
        else if(distance > g_brick_size * g_brick_size * g_brick_size) {
            debugPrintfEXT("encoding starts between LoDs are too far away");
            return false;
        }
    }

    // check palette start of first LoD being 0 and second LoD being 1
    if (CSGV_SHARED_MEMORY_BRICK_ENCODING[brick_start + header_start_lods] != 0u) {
        debugPrintfEXT("first palette start must be 0");
        return false;
    }
    if (CSGV_SHARED_MEMORY_BRICK_ENCODING[header_start_lods + 1u] != 1u) {
        debugPrintfEXT("second palette start must be 1");
        return false;
    }

//    // check palette starts being in ascending order
//    for(int l = 2u; l <= g_lod_count + 1; l++) {
//        if(brick_encoding[header_start_lods + l] < brick_encoding[header_start_lods + l - 1]) {
//            error << "  palette starts are not in ascending order\n";
//            break;
//        }
//    }

    uint palette_size = CSGV_SHARED_MEMORY_BRICK_ENCODING[getPaletteSizeHeaderIndex()];
    // check palette size not being zero
    if (palette_size == 0u) {
        debugPrintfEXT("palette size is zero");
        return false;
    }

//    // check palette size + encoding start of last LoD being shorter than the brick encoding
//    if (palette_size + brick_encoding[header_start_lods]/8u > brick_encoding_length) {
//        error << "  palette size and encoding of first (L-1) levels are longer than the total brick encoding\n";
//    }
    
    return true;
}

#endif // ENCODER_SERIAL_GLSL
