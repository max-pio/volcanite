//  Copyright (C) 2024, Max Piochowiak and Reiner Dolp, Karlsruhe Institute of Technology
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

namespace vvv {

/// Hash an arbitrary memory block of size byte_size starting at data.
/// @param combine_hash can be initialized with a hash to combine hashes
static size_t hashMemory(const void *data, size_t byte_size, size_t combine_hash = 0) {
    size_t hash = combine_hash;
    auto p = static_cast<const unsigned char *>(data);
    for (size_t i = 0; i < byte_size; i++)
        hash = (std::hash<unsigned char>{}(p[i]) ^ (std::rotl<size_t>(hash, 1)));
    return hash;
}

} // namespace vvv
