# RISCV
cmake_minimum_required(VERSION 3.23)

find_package(ZLIB)
set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

include(./generating_rules.cmake)

set(
        riscv_hdrs
        htif.h
        common.h
        decode.h
        mmu.h
        processor.h
        sim.h
        trap.h
        encoding.h
        cachesim.h
        memtracer.h
        extension.h
        rocc.h
        insn_template.h
        mulhi.h
        bbtracker.h
        gzstream.h
        ckpt_desc_reader.h
        debug_tracer.h
        pc_freqvec_tracker.h
        ${riscv_gen_hdrs}
)

set(
        riscv_srcs
        htif.cc
        processor.cc
        sim.cc
        interactive.cc
        trap.cc
        cachesim.cc
        mmu.cc
        disasm.cc
        extension.cc
        extensions.cc
        rocc.cc
        regnames.cc
        bbtracker.cc
        gzstream.cc
        ckpt_desc_reader.cc
        debug_tracer.cc
        ${riscv_gen_srcs}
)

add_library(riscv-interface INTERFACE)
target_link_libraries(riscv-interface INTERFACE ac_config)
target_sources(riscv-interface INTERFACE
        FILE_SET HEADERS
        BASE_DIRS . ${CMAKE_CURRENT_BINARY_DIR} # ${CMAKE_CURRENT_BINARY_DIR} is to include the generated sources
        FILES ${riscv_hdrs}
)

add_library(
        riscv STATIC
        ${riscv_srcs}
        ${riscv_hdrs}
)
target_link_libraries(riscv riscv-interface)
target_link_libraries(riscv fesvr-static)
target_link_libraries(riscv softfloat)
target_link_libraries(riscv ZLIB::ZLIB)
target_link_libraries(riscv Threads::Threads)
target_link_libraries(riscv ${CMAKE_DL_LIBS})
