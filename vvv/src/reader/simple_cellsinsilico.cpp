#include <vvv/volren/Volume.hpp>

#include <vulkan/vulkan.hpp>
#include <vvv/util/Logger.hpp>

#include <cmath>
#include <iostream>


namespace vvv {

template <typename T> std::shared_ptr<Volume<T>> load_simple_cellsinsilico_(std::string url, std::string formatLabel, size_t bitwidth, vk::Format gpuFormat) {
    std::ifstream nrrd(url, std::ios_base::in | std::ios_base::binary);
    if (!nrrd.is_open()) {
        std::ostringstream err;
        err << "unable to open cellsinsilico NRRD file at: " << url << "\n";
        Logger(ERROR) << err.str();
        throw std::runtime_error(err.str());
    }

    // read dimension
    int img_width = 0;
    int img_height = 0;
    int img_depth = 0;
    uint16_t bits_per_sample = 0;

    // read header
    std::string line;
    // first line contains space seperated width height depth
    if (!std::getline(nrrd, line)) {
        nrrd.close();
        throw std::runtime_error("unexpected end of file in " + url);
    }
    std::istringstream sizes(line);
    sizes >> img_width >> img_height >> img_depth;
    // second line contains data type
    if (!std::getline(nrrd, line)) {
        nrrd.close();
        throw std::runtime_error("unexpected end of file in " + url);
    }
    if(line != formatLabel)
        throw std::runtime_error("data type " + line + " does not equal to requested format " + formatLabel);
    if (line == "uint32")
        bits_per_sample = 32;
    else if (line == "uint16")
        bits_per_sample = 16;
    else if (line == "uint8")
        bits_per_sample = 8;
    else {
        nrrd.close();
        throw std::runtime_error("unexpected data type " + line + " in " + url);
    }

    // header is always two lines
    uint32_t lineNum = 2;

    float max_dim = static_cast<float>(std::max(img_width, std::max(img_height, img_depth)));
    float physical_size_x = static_cast<float>(img_width) / max_dim;
    float physical_size_y = static_cast<float>(img_height) / max_dim;
    float physical_size_z = static_cast<float>(img_depth) / max_dim;

    if (physical_size_x <= 0.f || physical_size_y <= 0.f || physical_size_z <= 0.f || !std::isfinite(physical_size_x) || !std::isfinite(physical_size_y)|| !std::isfinite(physical_size_z)) {
        nrrd.close();
        throw std::invalid_argument("invalid NRRD physical volume size");
    }

    // thats a 8GiB volume for 8bit samples, 16GiB for 16bit samples
    const uint64_t MAX_ALLOWED_VOXELS = 2048ul * 2048 * 2048;
    const uint64_t voxel_count = img_width * img_height * img_depth;

    if (MAX_ALLOWED_VOXELS < voxel_count) {
        nrrd.close();
        throw std::invalid_argument("NRRD volume exceeds maximum allowed size");
    }

    size_t byte_size = voxel_count * (bits_per_sample / 8);
    std::vector<T> payload(voxel_count);

    // read binary data inline
    nrrd.read(reinterpret_cast<char *>(payload.data()), byte_size);

    if (!nrrd) {
        nrrd.close();
        throw std::runtime_error("only " + std::to_string(nrrd.gcount()) + " bytes of expected " + std::to_string(byte_size) + " bytes could be read from NRRD file.");
    }

    nrrd.close();
    return std::make_shared<Volume<T>>(physical_size_x, physical_size_y, physical_size_z, img_width, img_height, img_depth, gpuFormat, payload);
}

template <typename T> std::shared_ptr<Volume<T>> load_simple_cellsinsilico_with_cast_(std::string url, std::string formatLabel, vk::Format gpuFormat) {
    throw std::runtime_error("load_simple_cellsinsilico_with_cast not implemented yet!");
    //
    //    std::ifstream nrrd(url, std::ios_base::in | std::ios_base::binary);
    //    if (!nrrd.is_open()) {
    //        std::ostringstream err;
    //        err << "unable to open NRRD file at: " << url << "\n";
    //        throw std::runtime_error(err.str());
    //    }
    //
    //    // TODO(Max): read fields with key/value pairs from nrrd file
    //    // read dimension
    //    int img_width = 0;
    //    int img_height = 0;
    //    int img_depth = 0;
    //    std::string payloadTy;
    //    uint32_t payloadComponentSize;
    //    float minVal = std::numeric_limits<float>::max();
    //    float maxVal = std::numeric_limits<float>::min();
    //
    //    std::optional<std::string> detachedPayload;
    //
    //    // read header
    //    uint32_t lineNum = 0;
    //    while (true) {
    //        std::string line;
    //        lineNum++;
    //
    //        if (!std::getline(nrrd, line)) {
    //            // EOF, could happen for detached headers
    //            break;
    //        }
    //
    //        if (line.size() == 0) {
    //            // empty line, end of header
    //            break;
    //        }
    //
    //        if (line[line.size() - 1] == '\r') {
    //            // windows line endings are allowed per standard, ignore extra carriage return if present
    //            line = line.substr(0, line.size() - 1);
    //        }
    //
    //        // optionally accept the header, required per standard but optional in our impl...
    //        if (lineNum == 1 && line == "NRRD0004") {
    //            continue;
    //        }
    //
    //        if (line[0] == '#') {
    //            // comment line
    //            continue;
    //        }
    //
    //        const std::string sepChars = ": ";
    //        const auto sep = line.find(sepChars);
    //
    //        if (sep == std::string::npos) {
    //            nrrd.close();
    //            throw std::runtime_error("invalid header in line" + std::to_string(lineNum) + ": " + line);
    //        }
    //
    //        // TODO: trim space
    //        const auto fieldName = line.substr(0, sep);
    //        const auto fieldValue = line.substr(sep + sepChars.size(), line.size() - sep - sepChars.size());
    //
    //        if (fieldName == "dimension") {
    //            // TODO: check the whole fieldValue is read/parsed,
    //            // TODO: check out of range
    //            const auto dim = std::stoul(fieldValue, nullptr, 10);
    //            if (dim != 3) {
    //                nrrd.close();
    //                throw std::runtime_error("expected 3 dimensions, got " + std::to_string(dim));
    //            }
    //        } else if (fieldName == "type") {
    //            if (fieldValue == "uint16") {
    //                payloadTy = fieldValue;
    //                payloadComponentSize = 2;
    //                minVal = std::numeric_limits<uint16_t>::min();
    //                maxVal = std::numeric_limits<uint16_t>::max();
    //            } else if (fieldValue == "uint8") {
    //                payloadTy = fieldValue;
    //                payloadComponentSize = 1;
    //                minVal = std::numeric_limits<uint8_t>::min();
    //                maxVal = std::numeric_limits<uint8_t>::max();
    //            } else {
    //                nrrd.close();
    //                throw std::runtime_error("expected uint8 or uint16 data type, got: " + fieldName);
    //            }
    //        } else if (fieldName == "encoding") {
    //            if (fieldValue != "raw") {
    //                nrrd.close();
    //                throw std::runtime_error("expected raw encoding, got: " + fieldValue);
    //            }
    //        } else if (fieldName == "endian") {
    //            if (fieldValue != "little") {
    //                nrrd.close();
    //                throw std::runtime_error("expected little endian, got: " + fieldValue);
    //            }
    //        } else if (fieldName == "data file") {
    //            detachedPayload = fieldValue;
    //        } else if (fieldName == "sizes") {
    //            // TODO(Reiner): check for end of line
    //            std::istringstream sizes(fieldValue);
    //            sizes >> img_width >> img_height >> img_depth;
    //        } else {
    //            std::cout << "ignoring unknown header field: " << fieldName << std::endl;
    //        }
    //    }
    //
    //    float max_dim = static_cast<float>(std::max(img_width, std::max(img_height, img_depth)));
    //    float physical_size_x = img_width / max_dim;
    //    float physical_size_y = img_height / max_dim;
    //    float physical_size_z = img_depth / max_dim;
    //
    //    if (!is_valid_physical_size(physical_size_x) || !is_valid_physical_size(physical_size_y) || !is_valid_physical_size(physical_size_z)) {
    //        nrrd.close();
    //        throw std::invalid_argument("invalid NRRD physical size");
    //    }
    //
    //    // thats a 8GiB volume for 8bit samples, 16GiB for 16bit samples
    //    const uint64_t MAX_ALLOWED_VOXELS = 2048ul * 2048 * 2048;
    //    const uint64_t voxel_count = img_width * img_height * img_depth;
    //
    //    if (MAX_ALLOWED_VOXELS < voxel_count) {
    //        nrrd.close();
    //        throw std::invalid_argument("NRRD volume exceeds maximum allowed size");
    //    }
    //
    //    size_t byte_size_raw = voxel_count * payloadComponentSize;
    //    std::vector<uint8_t> payloadRaw(byte_size_raw);
    //
    //    if (detachedPayload) {
    //        nrrd.close();
    //
    //        // standard has a weird definition of relative paths...
    //        const bool isRelative = !detachedPayload.value().empty() && detachedPayload.value()[0] != '/';
    //
    //        if (isRelative) {
    //            std::string directory = "";
    //            const size_t last_slash_idx = url.rfind('/');
    //            if (std::string::npos != last_slash_idx) {
    //                directory = url.substr(0, last_slash_idx + 1);
    //            }
    //            detachedPayload = directory + detachedPayload.value();
    //        }
    //
    //        nrrd = std::ifstream(detachedPayload.value(), std::ios_base::in | std::ios_base::binary);
    //
    //        if (!nrrd.is_open()) {
    //            std::ostringstream err;
    //            err << "unable to open detached payload of NRRD file <" << url << "> at <" << detachedPayload.value() << ">";
    //            throw std::runtime_error(err.str());
    //        }
    //    }
    //
    //    // read binary data inline
    //    nrrd.read(reinterpret_cast<char *>(payloadRaw.data()), byte_size_raw);
    //
    //    if (!nrrd) {
    //        nrrd.close();
    //        throw std::runtime_error("only " + std::to_string(nrrd.gcount()) + " bytes of expected " + std::to_string(byte_size_raw) + " bytes could be read from NRRD file.");
    //    }
    //
    //    nrrd.close();
    //
    //    std::vector<T> payload(voxel_count);
    //
    //    const auto needs_cast = formatLabel != payloadTy;
    //    if (needs_cast) {
    //        float tmin = std::numeric_limits<T>::min();
    //        float trange = static_cast<float>(std::numeric_limits<T>::max()) - tmin;
    //        if (payloadTy == "uint8") {
    //            for (int j = 0; j < voxel_count; ++j) {
    //                auto val = reinterpret_cast<uint8_t *>(payloadRaw.data())[j];
    //                auto cval = static_cast<T>(((static_cast<float>(val) - minVal) / (maxVal - minVal) * trange) + tmin);
    //                payload[j] = cval;
    //            }
    //        } else if (payloadTy == "uint16") {
    //            for (int j = 0; j < voxel_count; ++j) {
    //                // assumes the machine is using little endian
    //                auto val = reinterpret_cast<uint16_t *>(payloadRaw.data())[j];
    //                auto cval = static_cast<T>(((static_cast<float>(val) - minVal) / (maxVal - minVal) * trange) + tmin);
    //                payload[j] = cval;
    //            }
    //        } else {
    //            assert(false);
    //        }
    //
    //        return std::make_shared<Volume<T>>(physical_size_x, physical_size_y, physical_size_z, img_width, img_height, img_depth, gpuFormat, payload);
    //    } else {
    //        const auto first = reinterpret_cast<uint16_t *>(payloadRaw.data());
    //        return std::make_shared<Volume<T>>(physical_size_x, physical_size_y, physical_size_z, img_width, img_height, img_depth, gpuFormat, first, first + voxel_count);
    //    }
}

template <> std::shared_ptr<Volume<uint32_t>> Volume<uint32_t>::load_simple_cellsinsilico(std::string path, bool allowCast) {
    return allowCast ? load_simple_cellsinsilico_with_cast_<uint32_t>(path, "uint32", vk::Format::eR32Uint) : load_simple_cellsinsilico_<uint32_t>(path, "uint32", 32, vk::Format::eR32Uint);
}
template <> std::shared_ptr<Volume<uint16_t>> Volume<uint16_t>::load_simple_cellsinsilico(std::string path, bool allowCast) {
    return allowCast ? load_simple_cellsinsilico_with_cast_<uint16_t>(path, "uint16", vk::Format::eR16Uint) : load_simple_cellsinsilico_<uint16_t>(path, "uint16", 16, vk::Format::eR16Uint);
}
template <> std::shared_ptr<Volume<uint8_t>> Volume<uint8_t>::load_simple_cellsinsilico(std::string path, bool allowCast) {
    return allowCast ? load_simple_cellsinsilico_with_cast_<uint8_t>(path, "uint8", vk::Format::eR8Uint) : load_simple_cellsinsilico_<uint8_t>(path, "uint8", 8, vk::Format::eR8Uint);
}


template <> void Volume<uint32_t>::write_simple_cellsinsilico(std::string path) {
    std::ofstream nrrd(path, std::ios_base::out | std::ios_base::binary);
    if (!nrrd.is_open()) {
        std::ostringstream err;
        err << "unable to open cellsinsilico NRRD file at: " << path << "\n";
        Logger(ERROR) << err.str();
        throw std::runtime_error(err.str());
    }

    // read header
    std::string line = std::to_string(dim_x) + " " + std::to_string(dim_y) + " " + std::to_string(dim_z);
    nrrd << line << std::endl;
    nrrd << "uint32" << std::endl;
    // read binary data inline
    nrrd.write(reinterpret_cast<char *>(m_payload.data()), m_payload.size() * sizeof(uint32_t));

    nrrd.close();
}

} // namespace vvv
