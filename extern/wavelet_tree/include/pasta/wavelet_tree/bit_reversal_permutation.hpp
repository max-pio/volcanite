/*******************************************************************************
 * pasta/wavelet_tree/bit_reversal_permutation_lut.hpp
 *
 * Copyright (C) 2021 Florian Kurpicz <florian@kurpicz.org>
 *
 * pasta::wavelet_tree is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * pasta::wavelet_tree is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with pasta::wavelet_tree.  If not, see <http://www.gnu.org/licenses/>.
 *
 ******************************************************************************/

#pragma once

#include <array>

namespace pasta {

//! All bit-reversal permutations up to length 256 concatenated.
static constexpr std::array<size_t, 512> BitReversalPermutationData = {
    0,                                            // 0
    0,           1,                               // 1
    0,           2,   1,   3,                     // 2
    0,           4,   2,   6,   1,   5,   3,   7, // 3
    0,           8,   4,   12,  2,   10,  6,   14,  1,   9,   5,   13,
    3,           11,  7,   15, // 4
    0,           16,  8,   24,  4,   20,  12,  28,  2,   18,  10,  26,
    6,           22,  14,  30,
    /*cnt*/ 1,   17,  9,   25,  5,   21,  13,  29,  3,   19,  11,  27,
    7,           23,  15,  31, // 5
    0,           32,  16,  48,  8,   40,  24,  56,  4,   36,  20,  52,
    12,          44,  28,  60,  2,   34,  18,  50,
    /*cnt*/ 10,  42,  26,  58,  6,   38,  22,  54,  14,  46,  30,  62,
    1,           33,  17,  49,  9,   41,  25,  57,  5,   37,  21,  53,
    13,          45,  29,  61,  3,   35,  19,  51,
    /*cnt*/ 11,  43,  27,  59,  7,   39,  23,  55,  15,  47,  31,  63, // 6
    0,           64,  32,  96,  16,  80,  48,  112, 8,   72,  40,  104,
    24,          88,  56,  120, 4,   68,  36,
    /*cnt*/ 100, 20,  84,  52,  116, 12,  76,  44,  108, 28,  92,  60,
    124,         2,   66,  34,
    /*cnt*/ 98,  18,  82,  50,  114, 10,  74,  42,  106, 26,  90,  58,
    122,         6,   70,  38,
    /*cnt*/ 102, 22,  86,  54,  118, 14,  78,  46,  110, 30,  94,  62,
    126,         1,   65,  33,  97,  17,  81,  49,  113, 9,   73,  41,
    105,         25,  89,  57,  121, 5,   69,  37,
    /*cnt*/ 101, 21,  85,  53,  117, 13,  77,  45,  109, 29,  93,  61,
    125,         3,   67,  35,
    /*cnt*/ 99,  19,  83,  51,  115, 11,  75,  43,  107, 27,  91,  59,
    123,         7,   71,  39,
    /*cnt*/ 103, 23,  87,  55,  119, 15,  79,  47,  111, 31,  95,  63,
    127, // 7
    0,           128, 64,  192, 32,  160, 96,  224, 16,  144, 80,  208,
    48,          176, 112, 240, 8,
    /*cnt*/ 136, 72,  200, 40,  168, 104, 232, 24,  152, 88,  216, 56,
    184,         120, 248,
    /*cnt*/ 4,   132, 68,  196, 36,  164, 100, 228, 20,  148, 84,  212,
    52,          180, 116,
    /*cnt*/ 244, 12,  140, 76,  204, 44,  172, 108, 236, 28,  156, 92,
    220,         60,  188,
    /*cnt*/ 124, 252, 2,   130, 66,  194, 34,  162, 98,  226, 18,  146,
    82,          210, 50,
    /*cnt*/ 178, 114, 242, 10,  138, 74,  202, 42,  170, 106, 234, 26,
    154,         90,  218,
    /*cnt*/ 58,  186, 122, 250, 6,   134, 70,  198, 38,  166, 102, 230,
    22,          150, 86,
    /*cnt*/ 214, 54,  182, 118, 246, 14,  142, 78,  206, 46,  174, 110,
    238,         30,  158,
    /*cnt*/ 94,  222, 62,  190, 126, 254, 1,   129, 65,  193, 33,  161,
    97,          225, 17,  145, 81,  209, 49,  177, 113, 241, 9,
    /*cnt*/ 137, 73,  201, 41,  169, 105, 233, 25,  153, 89,  217, 57,
    185,         121, 249,
    /*cnt*/ 5,   133, 69,  197, 37,  165, 101, 229, 21,  149, 85,  213,
    53,          181, 117,
    /*cnt*/ 245, 13,  141, 77,  205, 45,  173, 109, 237, 29,  157, 93,
    221,         61,  189,
    /*cnt*/ 125, 253, 3,   131, 67,  195, 35,  163, 99,  227, 19,  147,
    83,          211, 51,
    /*cnt*/ 179, 115, 243, 11,  139, 75,  203, 43,  171, 107, 235, 27,
    155,         91,  219,
    /*cnt*/ 59,  187, 123, 251, 7,   135, 71,  199, 39,  167, 103, 231,
    23,          151, 87,
    /*cnt*/ 215, 55,  183, 119, 247, 15,  143, 79,  207, 47,  175, 111,
    239,         31,  159,
    /*cnt*/ 95,  223, 63,  191, 127, 255 // 8
};

//! Entry points to bit-reversal permutations of specific length, i.e., the
//! i-th entry corresponds to the bit-reversal permutation of length 2^i.
static constexpr std::array<size_t const *, 9> BitReversalPermutation = {
    &BitReversalPermutationData[0],  &BitReversalPermutationData[1],
    &BitReversalPermutationData[3],  &BitReversalPermutationData[7],
    &BitReversalPermutationData[15], &BitReversalPermutationData[31],
    &BitReversalPermutationData[63], &BitReversalPermutationData[127],
    &BitReversalPermutationData[255]};

} // namespace pasta

/******************************************************************************/
