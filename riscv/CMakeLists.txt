# RISCV
if ("${CMAKE_VERSION}" VERSION_GREATER "3.0.0")
    find_package(ZLIB)
    set(THREADS_PREFER_PTHREAD_FLAG ON)
    find_package(Threads REQUIRED)
endif ()


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

add_library(
        riscv STATIC
        ${riscv_srcs}
        ${riscv_hdrs}
)

target_include_directories(riscv PUBLIC .)
# for including the generated code
target_include_directories(riscv PRIVATE ${CMAKE_CURRENT_BINARY_DIR})

target_link_libraries(riscv ac_config)
target_link_libraries(riscv ${CMAKE_DL_LIBS})
target_link_libraries(riscv fesvr-static)
target_link_libraries(riscv softfloat)


if ("${CMAKE_VERSION}" VERSION_GREATER "3.0.0")
    target_link_libraries(riscv ZLIB::ZLIB)
    target_link_libraries(riscv Threads::Threads)
else ()
    target_link_libraries(riscv z)
    target_link_libraries(riscv pthread)
endif ()


#target_compile_options(riscv PRIVATE "-fPIC")
