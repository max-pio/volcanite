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

#include <vvv/volren/Volume.hpp>

#include <vulkan/vulkan.hpp>

#include "vvv/util/volume_data_types.hpp"
#include <cmath>
#include <fstream>
#include <sstream>

namespace vvv {

bool is_valid_physical_size(float v) { return v > 0.f && std::isfinite(v); }

template <typename T>
std::shared_ptr<Volume<T>> load_nrrd_(const std::string &url, const std::string &formatLabel, size_t bitwidth, vk::Format gpuFormat) {

    std::ifstream nrrd(url, std::ios_base::in | std::ios_base::binary);
    if (!nrrd.is_open()) {
        throw std::runtime_error("unable to open NRRD file at: " + url);
    }

    // read dimension
    int img_width = 0;
    int img_height = 0;
    int img_depth = 0;
    uint16_t bits_per_sample = 0;
    double spacing_width = 1.;
    double spacing_height = 1.;
    double spacing_depth = 1.;

    std::optional<std::string> detachedPayload;

    // read header
    uint32_t lineNum = 0;
    while (true) {
        std::string line;
        lineNum++;

        if (!std::getline(nrrd, line)) {
            // EOF, could happen for detached headers
            break;
        }

        if (line.empty()) {
            // empty line, end of header
            break;
        }

        if (line[line.size() - 1] == '\r') {
            // windows line endings are allowed per standard, ignore extra carriage return if present
            line = line.substr(0, line.size() - 1);
        }

        // optionally accept the header, required per standard but optional in our impl...
        if (lineNum == 1) {
            if (line != "NRRD0004")
                throw std::runtime_error("invalid NRRD magic not matchin xpected NRRD format NRRD0004: " + line);
            continue;
        }

        if (line[0] == '#') {
            // comment line
            continue;
        }

        // some software exports header lines with invalid ":" separator instead of ": ":
        const std::string sepStrings[2] = {": ", ":"};
        auto sep = std::string::npos;
        auto sepStrLength = 0;
        for (const auto &s : sepStrings) {
            sep = line.find(s);
            if (sep != std::string::npos) {
                sepStrLength = s.size();
                break;
            }
        }

        if (sep == std::string::npos) {
            nrrd.close();
            throw std::runtime_error("invalid NRRD header in line " + std::to_string(lineNum) + ": " + line);
        }

        const auto fieldName = line.substr(0, sep);
        const auto fieldValue = line.substr(sep + sepStrLength, line.size() - sep - sepStrLength);

        if (fieldName == "dimension") {
            // TODO: check the whole fieldValue is read/parsed, and check for out of range
            const auto dim = std::stoul(fieldValue, nullptr, 10);
            if (dim != 3) {
                nrrd.close();
                throw std::runtime_error("expected 3 dimensions, got " + std::to_string(dim));
            }
        } else if (fieldName == "type") {
            if (sizeof(T) == VolumeDataTypes::byteSizeOfUnsignedType(fieldName)) {
                bits_per_sample = bitwidth;
            } else {
                nrrd.close();
                std::string allowed_types = "expected type {";
                for (const auto &t : *VolumeDataTypes::getUnsignedTypesForByteSize(sizeof(T)))
                    allowed_types.append(t + ", ");
                allowed_types.pop_back();
                allowed_types.pop_back();
                allowed_types.append("} but got: " + fieldName);
                throw std::runtime_error(allowed_types);
            }
        } else if (fieldName == "encoding") {
            if (fieldValue != "raw") {
                nrrd.close();
                throw std::runtime_error("expected raw encoding, got: " + fieldValue);
            }
        } else if (fieldName == "endian") {
            if (fieldValue != "little") {
                nrrd.close();
                throw std::runtime_error("expected little endian, got: " + fieldValue);
            }
        } else if (fieldName == "data file") {
            detachedPayload = fieldValue;
        } else if (fieldName == "sizes") {
            std::istringstream sizes(fieldValue);
            sizes >> img_width >> img_height >> img_depth;
        } else if (fieldName == "spacings") {
            std::istringstream spacings(fieldValue);
            spacings >> spacing_width >> spacing_height >> spacing_depth;
        } else {
            std::cout << "ignoring unknown NRRD header field: " << fieldName << std::endl;
        }
    }

    float max_dim = static_cast<float>(std::max(img_width * spacing_width, std::max(img_height * spacing_height, img_depth * spacing_depth)));
    float physical_size_x = static_cast<float>(img_width * spacing_width / max_dim);
    float physical_size_y = static_cast<float>(img_height * spacing_height / max_dim);
    float physical_size_z = static_cast<float>(img_depth * spacing_depth / max_dim);

    if (!is_valid_physical_size(physical_size_x) || !is_valid_physical_size(physical_size_y) || !is_valid_physical_size(physical_size_z)) {
        nrrd.close();
        throw std::invalid_argument("invalid NRRD physical size");
    }

    // thats a 8GiB volume for 8bit samples, 16GiB for 16bit samples
    const uint64_t MAX_ALLOWED_VOXELS = 2048ul * 2048 * 2048;
    const uint64_t voxel_count = img_width * img_height * img_depth;

    if (MAX_ALLOWED_VOXELS < voxel_count) {
        nrrd.close();
        throw std::invalid_argument("NRRD volume exceeds maximum allowed size of " + std::to_string(MAX_ALLOWED_VOXELS) + " voxels.");
    }

    size_t byte_size = voxel_count * (bits_per_sample / 8);
    std::vector<T> payload(voxel_count);

    if (detachedPayload) {
        nrrd.close();

        // standard has a weird definition of relative paths...
        const bool isRelative = !detachedPayload.value().empty() && detachedPayload.value()[0] != '/';

        if (isRelative) {
            std::string directory;
            const size_t last_slash_idx = url.rfind('/');
            if (std::string::npos != last_slash_idx) {
                directory = url.substr(0, last_slash_idx + 1);
            }
            detachedPayload = directory + detachedPayload.value();
        }

        nrrd = std::ifstream(detachedPayload.value(), std::ios_base::in | std::ios_base::binary);

        if (!nrrd.is_open()) {
            throw std::runtime_error("unable to open detached payload of NRRD file <" + url + "> at <" + detachedPayload.value() + ">");
        }
    }

    // read binary data inline
    nrrd.read(reinterpret_cast<char *>(payload.data()), static_cast<long>(byte_size));

    if (!nrrd) {
        nrrd.close();
        throw std::runtime_error("only " + std::to_string(nrrd.gcount()) + " bytes of expected " + std::to_string(byte_size) + " bytes could be read from NRRD file.");
    }

    nrrd.close();
    return std::make_shared<Volume<T>>(physical_size_x, physical_size_y, physical_size_z, img_width, img_height, img_depth, gpuFormat, payload);
}

template <typename T>
std::shared_ptr<Volume<T>> load_nrrd_with_cast_(const std::string &url, const std::string &formatLabel, vk::Format gpuFormat) {

    std::ifstream nrrd(url, std::ios_base::in | std::ios_base::binary);
    if (!nrrd.is_open()) {
        throw std::runtime_error("unable to open NRRD file at: " + url);
    }

    // TODO: read fields with key/value pairs from nrrd file
    // read dimension
    uint64_t img_width = 0;
    uint64_t img_height = 0;
    uint64_t img_depth = 0;
    uint32_t payloadComponentSize;
    float minVal = std::numeric_limits<float>::max();
    float maxVal = std::numeric_limits<float>::min();
    double spacing_width = 1.;
    double spacing_height = 1.;
    double spacing_depth = 1.;

    std::optional<std::string> detachedPayload;

    // read header
    uint32_t lineNum = 0;
    while (true) {
        std::string line;
        lineNum++;

        if (!std::getline(nrrd, line)) {
            // EOF, could happen for detached headers
            break;
        }

        if (line.empty()) {
            // empty line, end of header
            break;
        }

        if (line[line.size() - 1] == '\r') {
            // windows line endings are allowed per standard, ignore extra carriage return if present
            line = line.substr(0, line.size() - 1);
        }

        // optionally accept the header, required per standard but optional in our impl...
        if (lineNum == 1 && line == "NRRD0004") {
            continue;
        }

        if (line[0] == '#') {
            // comment line
            continue;
        }

        // some software exports header lines with invalid ":" separator instead of ": ":
        const std::string sepStrings[2] = {": ", ":"};
        auto sep = std::string::npos;
        auto sepStrLength = 0;
        for (const auto &s : sepStrings) {
            sep = line.find(s);
            if (sep != std::string::npos) {
                sepStrLength = s.size();
                break;
            }
        }

        if (sep == std::string::npos) {
            nrrd.close();
            throw std::runtime_error("invalid NRRD header in line " + std::to_string(lineNum) + ": " + line);
        }

        const auto fieldName = line.substr(0, sep);
        const auto fieldValue = line.substr(sep + sepStrLength, line.size() - sep - sepStrLength);

        if (fieldName == "dimension") {
            // TODO: check the whole fieldValue is read/parsed, and check for out of range
            const auto dim = std::stoul(fieldValue, nullptr, 10);
            if (dim != 3) {
                nrrd.close();
                throw std::runtime_error("expected 3 dimensions, got " + std::to_string(dim));
            }
        } else if (fieldName == "type") {
            switch (VolumeDataTypes::byteSizeOfUnsignedType(fieldValue)) {
            case 8:
                payloadComponentSize = 8;
                minVal = std::numeric_limits<uint64_t>::min();
                maxVal = static_cast<float>(std::numeric_limits<uint64_t>::max());
                break;
            case 4:
                payloadComponentSize = 4;
                minVal = std::numeric_limits<uint32_t>::min();
                maxVal = static_cast<float>(std::numeric_limits<uint32_t>::max());
                break;
            case 2:
                payloadComponentSize = 2;
                minVal = std::numeric_limits<uint16_t>::min();
                maxVal = std::numeric_limits<uint16_t>::max();
                break;
            case 1:
                payloadComponentSize = 1;
                minVal = std::numeric_limits<uint8_t>::min();
                maxVal = std::numeric_limits<uint8_t>::max();
                break;
            default:
                nrrd.close();
                throw std::runtime_error("expected uint8, uint16, or uint32 data type, got: " + fieldValue);
            }
        } else if (fieldName == "encoding") {
            if (fieldValue != "raw") {
                nrrd.close();
                throw std::runtime_error("expected raw encoding, got: " + fieldValue);
            }
        } else if (fieldName == "endian") {
            if (fieldValue != "little") {
                nrrd.close();
                throw std::runtime_error("expected little endian, got: " + fieldValue);
            }
        } else if (fieldName == "data file") {
            detachedPayload = fieldValue;
        } else if (fieldName == "sizes") {
            std::istringstream sizes(fieldValue);
            sizes >> img_width >> img_height >> img_depth;
        } else if (fieldName == "spacings") {
            std::istringstream spacings(fieldValue);
            spacings >> spacing_width >> spacing_height >> spacing_depth;
        } else if (fieldName == "space") {
            // TODO 'space' field not applied when reading NRRD volume
        } else if (fieldName == "kinds") {
            // TODO 'kinds' field not applied when reading NRRD volume
        } else {
            Logger(Warn) << "ignoring unknown NRRD header field: " << fieldName;
        }
    }

    float max_dim = static_cast<float>(std::max(img_width * spacing_width, std::max(img_height * spacing_height, img_depth * spacing_depth)));
    float physical_size_x = static_cast<float>(img_width * spacing_width / max_dim);
    float physical_size_y = static_cast<float>(img_height * spacing_height / max_dim);
    float physical_size_z = static_cast<float>(img_depth * spacing_depth / max_dim);

    if (!is_valid_physical_size(physical_size_x) || !is_valid_physical_size(physical_size_y) || !is_valid_physical_size(physical_size_z)) {
        nrrd.close();
        throw std::invalid_argument("invalid NRRD physical size");
    }

    // thats a 8GiB volume for 8bit samples, 16GiB for 16bit samples
    const uint64_t MAX_ALLOWED_VOXELS = 2048ul * 2048 * 2048;
    const uint64_t voxel_count = img_width * img_height * img_depth;

    if (MAX_ALLOWED_VOXELS < voxel_count) {
        nrrd.close();
        throw std::invalid_argument("NRRD volume exceeds maximum allowed size");
    }

    size_t byte_size_raw = voxel_count * payloadComponentSize;
    std::vector<uint8_t> payloadRaw(byte_size_raw);

    if (detachedPayload) {
        nrrd.close();

        // standard has a weird definition of relative paths...
        const bool isRelative = !detachedPayload.value().empty() && detachedPayload.value()[0] != '/';

        if (isRelative) {
            std::string directory = "";
            const size_t last_slash_idx = url.rfind('/');
            if (std::string::npos != last_slash_idx) {
                directory = url.substr(0, last_slash_idx + 1);
            }
            detachedPayload = directory + detachedPayload.value();
        }

        nrrd = std::ifstream(detachedPayload.value(), std::ios_base::in | std::ios_base::binary);

        if (!nrrd.is_open()) {
            throw std::runtime_error("unable to open detached payload of NRRD file <" + url + "> at <" + detachedPayload.value() + ">");
        }
    }

    // read binary data inline
    nrrd.read(reinterpret_cast<char *>(payloadRaw.data()), byte_size_raw);

    if (!nrrd) {
        nrrd.close();
        throw std::runtime_error("only " + std::to_string(nrrd.gcount()) + " bytes of expected " + std::to_string(byte_size_raw) + " bytes could be read from NRRD file.");
    }

    nrrd.close();

    std::vector<T> payload(voxel_count);

    constexpr bool RESCALE_ON_CAST = false;
    const auto needs_cast = sizeof(T) != payloadComponentSize;
    if (needs_cast) {
        float tmin = std::numeric_limits<T>::min();
        float trange = static_cast<float>(std::numeric_limits<T>::max()) - tmin;
        if (payloadComponentSize == 1) {
            for (int j = 0; j < voxel_count; ++j) {
                auto val = reinterpret_cast<uint8_t *>(payloadRaw.data())[j];
                if (RESCALE_ON_CAST) {
                    auto cval = static_cast<T>(((static_cast<float>(val) - minVal) / (maxVal - minVal) * trange) +
                                               tmin);
                    payload[j] = cval;
                } else {
                    payload[j] = val;
                }
            }
        } else if (payloadComponentSize == 2) {
            for (int j = 0; j < voxel_count; ++j) {
                // assumes the machine is using little endian
                auto val = reinterpret_cast<uint16_t *>(payloadRaw.data())[j];
                if (RESCALE_ON_CAST) {
                    auto cval = static_cast<T>(((static_cast<float>(val) - minVal) / (maxVal - minVal) * trange) + tmin);
                    payload[j] = cval;
                } else {
                    payload[j] = val;
                }
            }
        } else if (payloadComponentSize == 4) {
            for (int j = 0; j < voxel_count; ++j) {
                // assumes the machine is using little endian
                auto val = reinterpret_cast<uint32_t *>(payloadRaw.data())[j];
                if (RESCALE_ON_CAST) {
                    auto cval = static_cast<T>(((static_cast<float>(val) - minVal) / (maxVal - minVal) * trange) + tmin);
                    payload[j] = cval;
                } else {
                    payload[j] = val;
                }
            }
        } else if (payloadComponentSize == 8) {
            for (int j = 0; j < voxel_count; ++j) {
                // assumes the machine is using little endian
                auto val = reinterpret_cast<uint64_t *>(payloadRaw.data())[j];
                if (RESCALE_ON_CAST) {
                    auto cval = static_cast<T>(((static_cast<float>(val) - minVal) / (maxVal - minVal) * trange) + tmin);
                    payload[j] = cval;
                } else {
                    payload[j] = val;
                }
            }
        } else {
            throw std::runtime_error("Unknown componenent byte size in NRRD file.");
        }

        return std::make_shared<Volume<T>>(physical_size_x, physical_size_y, physical_size_z, img_width, img_height, img_depth, gpuFormat, payload);
    } else {
        const auto first = reinterpret_cast<T *>(payloadRaw.data());
        return std::make_shared<Volume<T>>(physical_size_x, physical_size_y, physical_size_z, img_width, img_height, img_depth, gpuFormat, first, first + voxel_count);
    }
}

template <>
std::shared_ptr<Volume<uint32_t>> Volume<uint32_t>::load_nrrd(std::string path, bool allowCast) {
    return allowCast ? load_nrrd_with_cast_<uint32_t>(path, "uint32", vk::Format::eR32Uint) : load_nrrd_<uint32_t>(path, "uint32", 32, vk::Format::eR32Uint);
};
template <>
std::shared_ptr<Volume<uint16_t>> Volume<uint16_t>::load_nrrd(std::string path, bool allowCast) {
    return allowCast ? load_nrrd_with_cast_<uint16_t>(path, "uint16", vk::Format::eR16Uint) : load_nrrd_<uint16_t>(path, "uint16", 16, vk::Format::eR16Uint);
};
template <>
std::shared_ptr<Volume<uint8_t>> Volume<uint8_t>::load_nrrd(std::string path, bool allowCast) {
    return allowCast ? load_nrrd_with_cast_<uint8_t>(path, "uint8", vk::Format::eR8Uint) : load_nrrd_<uint8_t>(path, "uint8", 8, vk::Format::eR8Uint);
};

template <typename T>
void write_nrrd_(Volume<T> *volume, const std::string &path, bool separatePayloadFile) {
    const std::string pathmeta = path + (separatePayloadFile ? ".nhdr" : ".nrrd");
    const std::string formatLabel = VolumeDataTypes::getUnsignedTypeForByteSize(sizeof(T));

    std::ofstream meta(pathmeta, std::ios_base::out | std::ios_base::binary | std::ios_base::trunc);
    meta << "NRRD0004" << std::endl
         << "# Complete NRRD file format specification at:" << std::endl
         << "# http://teem.sourceforge.net/nrrd/format.html" << std::endl
         << "# http://teem.sourceforge.net/nrrd/format.html" << std::endl
         << "type: " << formatLabel << std::endl
         << "dimension: 3" << std::endl
         << "space: left-posterior-superior"
         << std::endl
         //<< "space directions: (1.0,0,0) (0,1.0,0) (0,0,1.0)" << std::endl
         << "kinds: domain domain domain" << std::endl
         << "sizes: " << volume->dim_x << " " << volume->dim_y << " " << volume->dim_z << std::endl
         << "endian: little" << std::endl
         << "encoding: raw" << std::endl;

    if (separatePayloadFile) {

        const std::string pathpayload = path + "_" + formatLabel + ".raw";

        std::string basename = pathpayload;

        const size_t last_slash_idx = basename.find_last_of("\\/");
        if (std::string::npos != last_slash_idx) {
            basename.erase(0, last_slash_idx + 1);
        }

        meta << "data file: " << basename << std::endl;

        std::ofstream payload(pathpayload, std::ios_base::out | std::ios_base::trunc | std::ios_base::binary);
        payload.write(volume->getRawData_const(), volume->memorySize());
        payload.close();
    } else {
        meta << std::endl;
        meta.write(volume->getRawData_const(), volume->memorySize());
    }

    meta.close();
}

template <>
void Volume<uint32_t>::write_nrrd(const std::string &path, bool separatePayloadFile) {
    write_nrrd_(this, path, separatePayloadFile);
}
template <>
void Volume<uint16_t>::write_nrrd(const std::string &path, bool separatePayloadFile) {
    write_nrrd_(this, path, separatePayloadFile);
}
template <>
void Volume<uint8_t>::write_nrrd(const std::string &path, bool separatePayloadFile) {
    write_nrrd_(this, path, separatePayloadFile);
}

} // namespace vvv
