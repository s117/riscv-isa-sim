cmake_minimum_required(VERSION 3.23)

project(riscv-isa-sim)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_EXTENSIONS OFF)

add_compile_options(-Wall -Wno-unused)

# When calling the configure script of isa-sim and fesvr, this path is used as:
#     configure --prefix=${PREFIX} ...
if (NOT DEFINED PREFIX)
    get_filename_component(PREFIX ${PROJECT_SOURCE_DIR}/../install-linux ABSOLUTE)
    message(WARNING "PREFIX is not defined, using default: ${PREFIX}")
else ()
    message("PREFIX is set to: ${PREFIX}")
endif ()


set(CMAKE_CXX_FLAGS_RELO3 "-O3 -g")
set(CMAKE_C_FLAGS_RELO3 "-O3 -g")

add_subdirectory(riscv-fesvr)

add_subdirectory(config)

add_subdirectory(softfloat)
add_subdirectory(riscv)
add_subdirectory(dummy_rocc)
add_subdirectory(hwacha)
add_subdirectory(rvc)
add_subdirectory(spike_main)
add_subdirectory(extensions)
