#include "vvv/volren/TransferFunction.hpp"

// automatically generated from paraview source code on 2021-10-06T12:51:08.295Z using the following script in the browser console:
// ```
// function generateCppPythonCode() {
//     cppCode = "// automatically generated from paraview source code on " + new Date().toISOString() + " using the following script in the browser console:\n";
//     cppCode += "// ```\n";
//     cppCode += "// " + generateCppPythonCode.toString().replaceAll("\n", "\n// ") + "\n"
//     cppCode += "// ```\n";
//     cppCode += "// and data taken form `https://raw.githubusercontent.com/Kitware/ParaView/115d91a15c2f3859c56d1882661b6b8704885c42/Remoting/Views/ColorMaps.json`\n"
//     for(tf of tfs) {
//         if(!tf["ColorSpace"]) { console.warn("unknown colorspace", tf["Name"]) }
//         if(tf["ColorSpace"] && tf["ColorSpace"].toLowerCase() == "rgb") {
//             name = tf["Name"].replaceAll(",", "").replaceAll(/[^a-zA-Z0-9]/g, "_").toLowerCase();
//             cppCode += "    m.attr(\"colormap_"+ name +"\") = vvv::colormaps::" + name + ";\n";
//         }
//     }
//
//     copy(cppCode);
//     return cppCode;
// }
// ```
// and data taken form `https://raw.githubusercontent.com/Kitware/ParaView/115d91a15c2f3859c56d1882661b6b8704885c42/Remoting/Views/ColorMaps.json`
m.attr("colormap_black_body_radiation") = vvv::colormaps::black_body_radiation;
m.attr("colormap_x_ray") = vvv::colormaps::x_ray;
m.attr("colormap_black_blue_and_white") = vvv::colormaps::black_blue_and_white;
m.attr("colormap_cold_and_hot") = vvv::colormaps::cold_and_hot;
m.attr("colormap_rainbow_desaturated") = vvv::colormaps::rainbow_desaturated;
m.attr("colormap_rainbow_uniform") = vvv::colormaps::rainbow_uniform;
m.attr("colormap_turbo") = vvv::colormaps::turbo;
m.attr("colormap_jet") = vvv::colormaps::jet;
m.attr("colormap_grayscale") = vvv::colormaps::grayscale;
m.attr("colormap_black_orange_and_white") = vvv::colormaps::black_orange_and_white;
m.attr("colormap_rainbow_blended_white") = vvv::colormaps::rainbow_blended_white;
m.attr("colormap_rainbow_blended_grey") = vvv::colormaps::rainbow_blended_grey;
m.attr("colormap_rainbow_blended_black") = vvv::colormaps::rainbow_blended_black;
m.attr("colormap_blue_to_yellow") = vvv::colormaps::blue_to_yellow;
m.attr("colormap_haze") = vvv::colormaps::haze;
m.attr("colormap_hsv") = vvv::colormaps::hsv;