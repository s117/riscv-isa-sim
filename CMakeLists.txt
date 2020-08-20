cmake_minimum_required(VERSION 3.1)

project(riscv-isa-sim)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_EXTENSIONS OFF)
add_compile_options(-Wall -Wno-unused)
set(SCRIPT_DIR ${PROJECT_SOURCE_DIR}/scripts)

# When calling the configure script of isa-sim and fesvr, this path is used as:
#     configure --prefix=${AC_CONFIGURE_PREFIX} ...
set(AC_CONFIGURE_PREFIX ${CMAKE_BINARY_DIR}/install)

set(
        ISA_SIM_AC_CONFIGURE_ARGS
        --prefix=${AC_CONFIGURE_PREFIX}
        --with-fesvr=${AC_CONFIGURE_PREFIX}
        --enable-simpoint
        --enable-dbg-trace
)

add_subdirectory(riscv-fesvr)

add_subdirectory(config)
add_subdirectory(softfloat)
add_subdirectory(riscv)
add_subdirectory(dummy_rocc)
add_subdirectory(hwacha)
add_subdirectory(rvc)
add_subdirectory(spike_main)
