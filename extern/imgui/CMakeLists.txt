cmake_minimum_required(VERSION 3.16)
project(Imgui)

# library target that we can link / include in the parent directory
add_library(imgui
    imconfig.h
    imgui.h
    imgui_impl_glfw.h
    imgui_impl_vulkan.h
    imgui_internal.h
    imgui_stdlib.h
    imstb_rectpack.h
    imstb_textedit.h
    imstb_truetype.h
#
    imgui.cpp
    imgui_demo.cpp
    imgui_draw.cpp
    imgui_impl_glfw.cpp
    imgui_impl_vulkan.cpp
    imgui_stdlib.cpp
    imgui_tables.cpp
    imgui_widgets.cpp
#
    imGuIZMO.quat/imGuIZMOquat.cpp
    imGuIZMO.quat/imGuIZMOquat.h
    imGuIZMO.quat/vgConfig.h
    imGuIZMO.quat/vGizmo.h
    imGuIZMO.quat/vGizmoMath.h
    imGuIZMO.quat/vgMath.h
#
    implot/implot.cpp
	implot/implot.h
	implot/implot_internal.h
	implot/implot_items.cpp
)

# imgui uses glm internally
find_package(glm REQUIRED)
if(NOT TARGET glm::glm AND TARGET glm)
	# if the modern CMake alias target does not exist, we define it ourselves.
	add_library(glm::glm ALIAS glm)
endif()

# required packages
find_package(Vulkan REQUIRED)

target_compile_definitions(imgui PUBLIC -DIMGUI=1 -DVGIZMO_USES_GLM -DIMGUIZMO_IMGUI_FOLDER=./)
target_include_directories(imgui PUBLIC ./)

target_link_libraries(imgui PRIVATE ${GLFW_LIBRARIES} Vulkan::Vulkan glm::glm)
