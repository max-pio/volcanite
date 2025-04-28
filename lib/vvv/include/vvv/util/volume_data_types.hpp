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

#include <string>
#include <unordered_map>
#include <vector>

namespace vvv {

class VolumeDataTypes {
    static const std::unordered_map<uint32_t, std::vector<std::string>> VOLUME_DATA_TYPES;

  public:
    /// \return the size of the given unsigned type in bytes, -1 if type is not known.
    static int byteSizeOfUnsignedType(const std::string &type_specifier) {
        for (int size = 1; size <= 8; size *= 2) {
            const auto &specifiers = VOLUME_DATA_TYPES.at(size);
            if (std::find(specifiers.begin(), specifiers.end(), type_specifier) != specifiers.end())
                return size;
        }

        return -1;
    }

    /// \return all supported type descriptors for
    static const std::vector<std::string> *getUnsignedTypesForByteSize(int byte_size) {
        if (!VOLUME_DATA_TYPES.contains(byte_size))
            return nullptr;
        else
            return &(VOLUME_DATA_TYPES.at(byte_size));
    }

    /// \return a type descriptor (uint8, uint16, uint32, uint64) with the given byte size. empty string if not known.
    static std::string getUnsignedTypeForByteSize(int byte_size) {
        auto types = getUnsignedTypesForByteSize(byte_size);
        if (types)
            return types->at(0);
        else
            return "";
    }
};

const std::unordered_map<uint32_t, std::vector<std::string>> VolumeDataTypes::VOLUME_DATA_TYPES = {
    {1, {"uint8", "uint8_t", "uchar", "unsigned char"}},
    {2, {"uint16", "uint16_t", "ushort", "unsigned short", "unsigned short int"}},
    {4, {"uint32", "uint32_t", "uint", "unsigned int"}},
    {8, {"uint64", "uint64_t", "ulonglong", "unsigned long long", "unsigned long long int"}}};

} // namespace vvv