cmake_minimum_required(VERSION 3.23)

# The installation prefix is RISCV_INSTALL_PREFIX. It used to be PREFIX, but CMake's compiler detection
# (CMakeCompilerIdDetection.cmake) sets and unsets a variable of that name, so a cache entry PREFIX (-DPREFIX=...)
# corrupts it: the compiler ID comes out empty, and so do the flags that depend on it, such as -fPIC and -std=.
# Take a PREFIX given that way as RISCV_INSTALL_PREFIX, and drop it before project() detects the compiler.
if (DEFINED CACHE{PREFIX})
    message(WARNING "PREFIX is deprecated, use RISCV_INSTALL_PREFIX instead (now set to ${PREFIX})")
    set(RISCV_INSTALL_PREFIX "${PREFIX}" CACHE PATH "Installation prefix of the RISC-V tools" FORCE)
    unset(PREFIX CACHE)
endif ()

project(riscv-isa-sim)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_EXTENSIONS OFF)

add_compile_options(-Wall -Wno-unused)

# When calling the configure script of isa-sim and fesvr, this path is used as:
#     configure --prefix=${RISCV_INSTALL_PREFIX} ...
if (NOT DEFINED RISCV_INSTALL_PREFIX)
    get_filename_component(RISCV_INSTALL_PREFIX ${PROJECT_SOURCE_DIR}/../install-linux ABSOLUTE)
    message(WARNING "RISCV_INSTALL_PREFIX is not defined, using default: ${RISCV_INSTALL_PREFIX}")
else ()
    message("RISCV_INSTALL_PREFIX is set to: ${RISCV_INSTALL_PREFIX}")
endif ()


set(CMAKE_CXX_FLAGS_RELO3 "-O3 -g")
set(CMAKE_C_FLAGS_RELO3 "-O3 -g")

set(CMAKE_CXX_FLAGS_RELOG "-Og -g")
set(CMAKE_C_FLAGS_RELOG "-Og -g")

add_subdirectory(riscv-fesvr)

add_subdirectory(config)

add_subdirectory(softfloat)
add_subdirectory(riscv)
add_subdirectory(dummy_rocc)
add_subdirectory(hwacha)
add_subdirectory(rvc)
add_subdirectory(spike_main)
add_subdirectory(extensions)

if (DEFINED NO_DUFF_DEVICE)
    target_compile_definitions(riscv PUBLIC STEPPING_WITHOUT_DUFF_DEVICE)
endif ()

if (NOT DEFINED NO_SIMPOINT)
    target_compile_definitions(riscv PUBLIC RISCV_ENABLE_SIMPOINT)
endif ()
