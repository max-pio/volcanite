/*******************************************************************************
 * wavelet_tree_test.cop
 *
 * Copyright (C) 2019-2021 Florian Kurpicz <florian@kurpicz.org>
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

#include <algorithm>
#include <random>
#include <vector>

#include <tlx/die.hpp>

#include <pasta/wavelet_tree/wavelet_tree.hpp>
#include <pasta/utils/reduce_alphabet.hpp>

int32_t main() {

  std::random_device rnd_device;
  std::mt19937 mersenne_engine(rnd_device());
  std::uniform_int_distribution<uint8_t> dist;

  std::vector<uint8_t> text(5'000'000);
  std::vector<uint8_t> const text_copy = text;
  std::generate(text.begin(), text.end(),
                [&](){ return dist(mersenne_engine); });

  std::array<size_t, std::numeric_limits<
    typename decltype(text)::value_type>::max() + 1> alphabet_mapping;
  size_t alphabet_size = pasta::reduce_alphabet(text.begin(), text.end(),
                                                &alphabet_mapping);

  auto wt = pasta::make_wt<pasta::BitVector>(text.begin(), text.end(),
					       alphabet_size);

  std::array<size_t, 256> occ = { 0 };

  for (size_t i = 0; i < text.size(); ++i) {
    auto const result = wt[i];
    die_unequal(static_cast<size_t>(result), alphabet_mapping[text[i]]);
    auto const char_occ = ++occ[result];
    die_unequal(char_occ, wt.rank(i + 1, result));
    auto const pos = wt.select(char_occ, result);
    die_unequal(i, pos);
  }

  auto wm = pasta::make_wm<pasta::BitVector>(text.begin(), text.end(),
                                             alphabet_size);

  for (size_t i = 0; i < text.size() / 1000; i += 1000) {
    size_t const res = (size_t)wm[i];
    die_unequal(res, alphabet_mapping[text[i]]);
  }

  for (auto& o : occ) {
    o = 0;
  }

  for (size_t i = 0; i < text.size(); ++i) {
    auto const result = wt[i];
    die_unequal(static_cast<size_t>(result), alphabet_mapping[text[i]]);
    auto const char_occ = ++occ[result];
    die_unequal(char_occ, wm.rank(i + 1, result));
    auto const pos = wm.select(char_occ, result);
    die_unequal(i, pos);
  }

  return 0;
}

/******************************************************************************/
