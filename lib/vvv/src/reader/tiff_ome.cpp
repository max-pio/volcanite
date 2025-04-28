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

#include "vvv/volren/Volume.hpp"

#if defined(LIB_TIFF) && defined(LIB_PUGIXLM)
#include <cassert>
#include <cmath>
#include <cstring>
#include <iostream>
#include <pugixml.hpp>
#include <sstream>
#include <stdexcept>
#include <tiff.h>
#include <tiffio.h>

#define TIFF_FIELD(tif, name, ref...)                              \
    {                                                              \
        if (TIFFGetField(tif, name, ref) != 1) {                   \
            std::ostringstream err;                                \
            err << "unable to read TIFF field: " << #name << "\n"; \
            throw std::runtime_error(err.str());                   \
        }                                                          \
    }

bool is_valid_physical_size(float v) { return v > 0.f && std::isfinite(v); }
#endif

template <>
std::shared_ptr<vvv::Volume<uint32_t>> vvv::Volume<uint32_t>::load_ome_tiff(std::string url) {
#if defined(LIB_TIFF) && defined(LIB_PUGIXLM)
    TIFF *tif = TIFFOpen(url.c_str(), "r");

    if (tif == nullptr) {
        std::ostringstream err;
        err << "unable to open TIFF file at: " << url << "\n";
        throw std::runtime_error(err.str());
    }

    char *image_description = nullptr;

    uint16_t bits_per_sample = 0;

    uint32_t img_width = 0;
    uint32_t img_height = 0;

    uint16_t pagenumber = 0;
    uint16_t pagecount = 0;

    float physical_size_x = 0;
    float physical_size_y = 0;
    float physical_size_z = 0;

    TIFF_FIELD(tif, TIFFTAG_BITSPERSAMPLE, &bits_per_sample);
    TIFF_FIELD(tif, TIFFTAG_IMAGEWIDTH, &img_width);
    TIFF_FIELD(tif, TIFFTAG_IMAGELENGTH, &img_height);
    TIFF_FIELD(tif, TIFFTAG_PAGENUMBER, &pagenumber, &pagecount);
    TIFF_FIELD(tif, TIFFTAG_IMAGEDESCRIPTION, &image_description);

    if (bits_per_sample != 32) {
        TIFFClose(tif);
        throw std::invalid_argument("expected precision of 32 bit per sample");
    }

    // thats a 32GiB volume for 32bit samples
    const uint64_t MAX_ALLOWED_VOXELS = 2048ul * 2048 * 2048;
    const uint64_t voxel_count = img_width * img_height * pagecount;

    if (MAX_ALLOWED_VOXELS < voxel_count) {
        TIFFClose(tif);
        throw std::invalid_argument("TIFF image exceeds maximum allowed size");
    }

    pugi::xml_document doc;
    pugi::xml_parse_result result = doc.load_string(image_description);

    if (!result) {
        TIFFClose(tif);
        throw std::invalid_argument("failed to parse OME metadata");
    }

    const auto pixels_metadata = doc.select_node("/OME/Image/Pixels").node();

    if (!pixels_metadata) {
        TIFFClose(tif);
        throw std::invalid_argument("invalid or missing OME pixel description");
    }

    physical_size_x = pixels_metadata.attribute("PhysicalSizeX").as_float();
    physical_size_y = pixels_metadata.attribute("PhysicalSizeY").as_float();
    physical_size_z = pixels_metadata.attribute("PhysicalSizeZ").as_float();
    const auto pixel_type = pixels_metadata.attribute("Type").as_string();

    if (0 != strcmp("uint32", pixel_type)) {
        TIFFClose(tif);
        throw std::invalid_argument("expected uint32 samples");
    }

    if (!is_valid_physical_size(physical_size_x) || !is_valid_physical_size(physical_size_y) || !is_valid_physical_size(physical_size_z)) {
        TIFFClose(tif);
        throw std::invalid_argument("invalid physical size");
    }

    // Check if using _TIFFmalloc(voxel_count) would provide any benefits
    std::vector<uint32_t> payload(voxel_count); // new char[voxel_count * (bits_per_sample / 8)];

    tdata_t buf;
    const auto stripsize = TIFFStripSize(tif);
    const auto stripcount = TIFFNumberOfStrips(tif);
    buf = _TIFFmalloc(stripsize);
    int dircount = 0;
    const auto directroysize = sizeof(uint32_t) * img_width * img_height;
    assert(directroysize == (stripsize * stripcount));

    do {
        for (tstrip_t strip = 0; strip < stripcount; strip++) {
            TIFFReadEncodedStrip(tif, strip, buf, (tsize_t)-1);
            const size_t offset = stripsize * strip + dircount * directroysize;
            _TIFFmemcpy(payload.data() + offset, buf, stripsize);
        }
        dircount++;
    } while (TIFFReadDirectory(tif));

    _TIFFfree(buf);

    TIFFClose(tif);

    return std::make_shared<Volume>(physical_size_x, physical_size_y, physical_size_z, img_width, img_height, pagecount, vk::Format::eR32Uint, payload);
#else
    throw std::runtime_error("TIFF or PUGIXML libraries not found! Can not load TIFF volume file!");
    return nullptr;
#endif
}
