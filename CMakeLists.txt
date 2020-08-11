cmake_minimum_required(VERSION 3.1)

project(riscv-isa-sim)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_EXTENSIONS OFF)
add_compile_options(-Wall -Wno-unused)
set(SCRIPT_DIR ${PROJECT_SOURCE_DIR}/scripts)

# When calling the configure script of isa-sim and fesvr, this path is used as:
#     configure --prefix=${AC_CONFIGURE_PREFIX} ...
set(AC_CONFIGURE_PREFIX ${CMAKE_BINARY_DIR}/install)

# if riscv-fesvr project is not here, update the line below
set(FESVR_DIR ${PROJECT_SOURCE_DIR}/../riscv-fesvr)

set(ISA_SIM_AC_CONFIGURE_ARGS --prefix=${AC_CONFIGURE_PREFIX} --with-fesvr=${AC_CONFIGURE_PREFIX} --enable-simpoint)

if (NOT IS_DIRECTORY ${FESVR_DIR})
    message(FATAL_ERROR "The source tree for riscv-fesvr doesn't exist: '${FESVR_DIR}'")
else ()
    message(STATUS "Using riscv-fesvr from '${FESVR_DIR}'")
endif ()

add_subdirectory(fesvr)

add_subdirectory(config)
add_subdirectory(softfloat)
add_subdirectory(riscv)
add_subdirectory(dummy_rocc)
add_subdirectory(hwacha)
add_subdirectory(rvc)
add_subdirectory(spike_main)
