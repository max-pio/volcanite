cmake_minimum_required(VERSION 3.16)
project(examples VERSION 0.1.0 DESCRIPTION "These are examples on how to use the VVV framework" LANGUAGES CXX)

# find additional libraries that are not part of libvvv and libvvvwindow
# find_package([...])

set(EXAMPLES_HEADERS )

set(EXAMPLES_SOURCES )

makeExecutable(example_transfer_functions ${EXAMPLES_HEADERS} ${EXAMPLES_SOURCES})
makeExecutable(example_graphics_pass ${EXAMPLES_HEADERS} ${EXAMPLES_SOURCES})
makeExecutable(example_point_cloud ${EXAMPLES_HEADERS} ${EXAMPLES_SOURCES})
makeExecutable(sandbox ${EXAMPLES_HEADERS} ${EXAMPLES_SOURCES})

# target_link_libraries(<executable> PRIVATE [...])

set(EXAMPLES_ADDITIONAL_DATA_DIRS )

installExecutable(example_transfer_functions ${EXAMPLES_ADDITIONAL_DATA_DIRS})
installExecutable(example_graphics_pass ${EXAMPLES_ADDITIONAL_DATA_DIRS})
installExecutable(example_point_cloud ${EXAMPLES_ADDITIONAL_DATA_DIRS})
installExecutable(sandbox ${EXAMPLES_ADDITIONAL_DATA_DIRS})