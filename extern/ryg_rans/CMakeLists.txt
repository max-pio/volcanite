cmake_minimum_required(VERSION 3.16)
project(RygrANS CXX)

# library target that we can link / include in the parent directory
add_library(libryg-rans
#    rans64.h
    platform.h
        rans_nibble.h
#    rans_byte.h
#    rans_word_sse41.h
#
    main.cpp
#    main64.cpp
#    main_simd.cpp
#    main_alias.cpp
)

# required packages
target_compile_definitions(libryg-rans PUBLIC -DRYG_RANS_LIB=1)
target_include_directories(libryg-rans PUBLIC )

