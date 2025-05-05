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

#include "volcanite/compression/encoder/RangeANSEncoder.hpp"
#include "volcanite/compression/pack_rans.hpp"

namespace volcanite {

uint32_t RangeANSEncoder::readNextLodOperationFromEncoding(const uint32_t *brick_encoding, ReadState &state) const {
    const RANS *rans = state.in_detail_lod ? &m_detail_rans : &m_rans;
    return rans->itr_nextSymbol(state.rans_state, state.idxE, brick_encoding);
}

} // namespace volcanite
