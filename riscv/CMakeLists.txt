# RISCV
find_package(ZLIB)
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
target_link_libraries(riscv ZLIB::ZLIB)
target_link_libraries(riscv fesvr)
target_link_libraries(riscv softfloat)

#target_compile_options(riscv PRIVATE "-fPIC")
