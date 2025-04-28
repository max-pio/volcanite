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

#include <vvv/util/Logger.hpp>
#include <vvv/volren/Volume.hpp>

#include <vulkan/vulkan.hpp>

#ifdef LIB_VTK
#include <vtkCellData.h>
#include <vtkDataArrayRange.h>
#include <vtkImageData.h>
#include <vtkIntArray.h>
#include <vtkSmartPointer.h>
#include <vtkXMLImageDataReader.h>
#include <vtkXMLParser.h>
#endif

#include <cmath>

namespace vvv {

uint32_t swapEndian(uint32_t v) {
    uint32_t b0, b1, b2, b3;
    b0 = (v & 0x000000ff) << 24u;
    b1 = (v & 0x0000ff00) << 8u;
    b2 = (v & 0x00ff0000) >> 8u;
    b3 = (v & 0xff000000) >> 24u;
    return b0 | b1 | b2 | b3;
}

std::string readParameterFromHeader(std::string line, std::string parameter) {
    size_t pos = line.find(parameter + "=\"");
    if (pos == std::string::npos)
        return "";
    std::string s = line.substr(pos + parameter.length() + 2);
    pos = s.find("\"");
    if (pos == std::string::npos)
        return "";
    s = s.substr(0, pos);
    return s;
}

template <typename T>
std::shared_ptr<Volume<T>> load_nastja_volume_from_vti(std::string url, std::string formatLabel, vk::Format gpuFormat) {
    std::ifstream file(url, std::ios_base::in | std::ios_base::binary);
    if (!file.is_open()) {
        std::ostringstream err;
        err << "unable to open vti file at: " << url << "\n";
        Logger(Error) << err.str();
        throw std::runtime_error(err.str());
    }

    // read dimension
    uint64_t img_width = 0;
    uint64_t img_height = 0;
    uint64_t img_depth = 0;
    float physical_size_x;
    float physical_size_y;
    float physical_size_z;

    /* We expect a vtk file to look like this:
    <VTKFile type="ImageData" version="0.1" byte_order="LittleEndian" header_type="UInt64">
    <ImageData WholeExtent="0 400 0 400 0 402" Origin="0 0 0" Spacing="1.000000e+00 1.000000e+00 1.000000e+00">
    <CellData Scalars="cells">
    <DataArray type="UInt32" Name="cells" format="appended" offset="0" NumberOfComponents="1"/>
    </CellData>
    </ImageData>
    <AppendedData encoding="raw">
    [[[RAW ARRAY INPUT]]]
    </AppendedData>
    </VTKFile>
    */

    // read header:
    std::string line;
    // first line contains the VTKFile header
    do {
        if (!std::getline(file, line)) {
            file.close();
            throw std::runtime_error("unexpected end of file in " + url);
        }
    } while (line.find("VTKFile") == std::string::npos);
    std::string byteOrder = readParameterFromHeader(line, "byte_order");
    if (readParameterFromHeader(line, "type") != "ImageData" || (byteOrder != "LittleEndian" && byteOrder != "BigEndian")) {
        file.close();
        throw std::runtime_error("Invalid .vti file header, expected type ImageData, and byte_order LittleEndian or BigEndian.");
    }
    // second line contains the ImageData header
    if (!std::getline(file, line)) {
        file.close();
        throw std::runtime_error("unexpected end of file in " + url);
    }
    if (6 != std::sscanf(line.c_str(), "<ImageData WholeExtent=\"0 %lu 0 %lu 0 %lu\" Origin=\"0 0 0\" Spacing=\"%f %f %f\">", &img_width, &img_height, &img_depth,
                         &physical_size_x, &physical_size_y, &physical_size_z)) {
        file.close();
        throw std::runtime_error("Could not read <ImageData ..> header from second line in .vti file " + url);
    }

    // fourth line contains DataArray header: <DataArray type="UInt32" Name="cells" format="appended" offset="0" NumberOfComponents="1"/>
    if (!std::getline(file, line)) {
        file.close();
        throw std::runtime_error("unexpected end of file in " + url);
    }
    if (!std::getline(file, line)) {
        file.close();
        throw std::runtime_error("unexpected end of file in " + url);
    }
    if (readParameterFromHeader(line, "type") != formatLabel || readParameterFromHeader(line, "format") != "appended" || readParameterFromHeader(line, "offset") != "0" || readParameterFromHeader(line, "NumberOfComponents") != "1") {
        file.close();
        throw std::runtime_error("Invalid DataArray header, expected type " + formatLabel + ", format appended, offset 0, and NumberOfComponents 1 in line 4 of .vti file " + url);
    }

    // Actually, the physical dimension would be
    //   physical_size_x = physical_size_x * static_cast<float>(img_width) etc.
    // But we overwrite the physical dimension so that everything would be normalized with the maximum dimension set to 1.
    float max_dim = static_cast<float>(std::max(img_width, std::max(img_height, img_depth)));
    physical_size_x = static_cast<float>(img_width) / max_dim;
    physical_size_y = static_cast<float>(img_height) / max_dim;
    physical_size_z = static_cast<float>(img_depth) / max_dim;

    if (physical_size_x <= 0.f || physical_size_y <= 0.f || physical_size_z <= 0.f || !std::isfinite(physical_size_x) || !std::isfinite(physical_size_y) || !std::isfinite(physical_size_z)) {
        file.close();
        throw std::invalid_argument("invalid .vti physical volume size");
    }

    // thats a 8GiB volume for 8bit samples, 16GiB for 16bit samples
    const uint64_t MAX_ALLOWED_VOXELS = 2048ul * 2048 * 2048;
    const uint64_t voxel_count = img_width * img_height * img_depth;

    if (MAX_ALLOWED_VOXELS < voxel_count) {
        file.close();
        throw std::runtime_error(".vti volume exceeds maximum allowed size of " + std::to_string(MAX_ALLOWED_VOXELS) + " voxels.");
    }

    uint32_t bytes_per_sample = sizeof(T);
    size_t byte_size = voxel_count * bytes_per_sample;
    std::vector<T> payload(voxel_count);

    // read binary data inline
    if (!std::getline(file, line)) {
        file.close();
        throw std::runtime_error("unexpected end of file in " + url);
    }
    if (!std::getline(file, line)) {
        file.close();
        throw std::runtime_error("unexpected end of file in " + url);
    }
    if (!std::getline(file, line)) {
        file.close();
        throw std::runtime_error("unexpected end of file in " + url);
    }
    if (readParameterFromHeader(line, "encoding") != "raw") {
        file.close();
        throw std::runtime_error("Expected encoding 'raw' but got '" + readParameterFromHeader(line, "encoding") + "' in .vti file " + url);
    }

    // we have to read a single byte before the raw data starts
    char offset;
    file.read(&offset, 1);

    file.read(reinterpret_cast<char *>(payload.data()), byte_size);
    if (!file) {
        file.close();
        throw std::runtime_error("only " + std::to_string(file.gcount()) + " bytes of expected " + std::to_string(byte_size) + " bytes could be read from NRRD file.");
    }

    if (byteOrder == "BigEndian")
        std::transform(std::begin(payload), std::end(payload), std::begin(payload), swapEndian);

    file.close();
    return std::make_shared<Volume<T>>(physical_size_x, physical_size_y, physical_size_z, img_width, img_height, img_depth, gpuFormat, payload);
}

template <typename T>
std::shared_ptr<Volume<T>> load_volume_from_vti(std::string url, std::string formatLabel, vk::Format gpuFormat) {
#ifdef LIB_VTK
    vtkSmartPointer<vtkXMLImageDataReader> reader = vtkSmartPointer<vtkXMLImageDataReader>::New();
    if (!reader->CanReadFile(url.c_str()))
        throw std::runtime_error("XML image data reader can not read file " + url);

    reader->SetFileName(url.c_str());
    reader->Update();

    vtkSmartPointer<vtkImageData> vti_image = reader->GetOutput();
    vtkSmartPointer<vtkCellData> vti_cell = vti_image->GetCellData();
    if (!vti_cell)
        throw std::runtime_error("could not load cell data from vti file");
    vtkSmartPointer<vtkDataArray> vti_data = vti_cell->GetArray(0);
    if (!vti_data)
        throw std::runtime_error("could not load cell data array from vti file");

    int expected_vtk_type = -1;
    if (formatLabel == "UInt8")
        expected_vtk_type = VTK_UNSIGNED_CHAR;
    else if (formatLabel == "UInt16")
        expected_vtk_type = VTK_UNSIGNED_SHORT;
    else if (formatLabel == "UInt32")
        expected_vtk_type = VTK_UNSIGNED_INT;
    else if (formatLabel == "UInt64")
        expected_vtk_type = VTK_UNSIGNED_LONG;
    else
        throw std::runtime_error("Data type " + formatLabel + " not yet supported for .vti import");

    if (vti_data->GetDataType() != expected_vtk_type) {
        throw std::runtime_error("Expected .vti data type " + formatLabel + " (vtkType " + std::to_string(expected_vtk_type) + ") but got vtkType " + std::to_string(vti_data->GetDataType()));
    }

    int img_dims[3];
    vti_image->GetDimensions(img_dims);
    for (int &img_dim : img_dims)
        img_dim = img_dim - 1;

    // copy the data
    std::vector<T> payload(img_dims[0] * img_dims[1] * img_dims[2]);
    const auto range = vtk::DataArrayValueRange<1>(vti_data);
    std::copy(range.cbegin(), range.cend(), payload.begin());

    // TODO: read physical size from the vti data property
    float max_dim = static_cast<float>(std::max(img_dims[0], std::max(img_dims[1], img_dims[2])));
    float physical_size_x = static_cast<float>(img_dims[0]) / max_dim;
    float physical_size_y = static_cast<float>(img_dims[1]) / max_dim;
    float physical_size_z = static_cast<float>(img_dims[2]) / max_dim;

    if (physical_size_x <= 0.f || physical_size_y <= 0.f || physical_size_z <= 0.f || !std::isfinite(physical_size_x) || !std::isfinite(physical_size_y) || !std::isfinite(physical_size_z)) {
        throw std::invalid_argument("invalid .vti physical volume size");
    }

    return std::make_shared<Volume<T>>(physical_size_x, physical_size_y, physical_size_z, img_dims[0], img_dims[1], img_dims[2], gpuFormat, payload);
#else
    Logger(Warn) << "VTK library not found. Using hardcoded vti import, expecting file layout:\n";
    Logger(Warn) << "<VTKFile type=\"ImageData\" version=\"0.1\" byte_order=\"LittleEndian\" header_type=\"UInt64\">";
    Logger(Warn) << "<ImageData WholeExtent=\"0 [WIDTH] 0 [HEIGHT] 0 [DEPTH]\" Origin=\"0 0 0\" Spacing=\"1.000000e+00 1.000000e+00 1.000000e+00\">";
    Logger(Warn) << "<CellData Scalars=\"[...]\">";
    Logger(Warn) << "<DataArray type=\"UInt32\" Name=\"[...]\" format=\"appended\" offset=\"0\" NumberOfComponents=\"1\"/>";
    Logger(Warn) << "</CellData>";
    Logger(Warn) << "</ImageData>";
    Logger(Warn) << "<AppendedData encoding=\"raw\">";
    Logger(Warn) << "[[[RAW ARRAY INPUT]]]";
    Logger(Warn) << "</AppendedData>";
    Logger(Warn) << "</VTKFile>";
    return load_nastja_volume_from_vti<T>(url, formatLabel, gpuFormat);
#endif
}

template <>
std::shared_ptr<Volume<uint8_t>> Volume<uint8_t>::load_vti(std::string path, bool allowCast) {
    assert(!allowCast && "Casting not yet supported for vti volume loaders.");
    return load_volume_from_vti<uint8_t>(path, "UInt8", vk::Format::eR8Uint);
}
template <>
std::shared_ptr<Volume<uint16_t>> Volume<uint16_t>::load_vti(std::string path, bool allowCast) {
    assert(!allowCast && "Casting not yet supported for vti volume loaders.");
    return load_volume_from_vti<uint16_t>(path, "UInt16", vk::Format::eR16Uint);
}
template <>
std::shared_ptr<Volume<uint32_t>> Volume<uint32_t>::load_vti(std::string path, bool allowCast) {
    assert(!allowCast && "Casting not yet supported for vti volume loaders.");
    return load_volume_from_vti<uint32_t>(path, "UInt32", vk::Format::eR32Uint);
}
template <>
std::shared_ptr<Volume<uint64_t>> Volume<uint64_t>::load_vti(std::string path, bool allowCast) {
    assert(!allowCast && "Casting not yet supported for vti volume loaders.");
    return load_volume_from_vti<uint64_t>(path, "UInt64", vk::Format::eR64Uint);
}

} // namespace vvv
