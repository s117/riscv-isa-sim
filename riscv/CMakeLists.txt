# RISCV
find_package(ZLIB)
include(./generating_rules.cmake)

set(
        riscv_hdrs
        common.h
        decode.h
        devices.h
        disasm.h
        dts.h
        mmu.h
        processor.h
        sim.h
        simif.h
        trap.h
        encoding.h
        cachesim.h
        memtracer.h
        mmio_plugin.h
        tracer.h
        extension.h
        rocc.h
        insn_template.h
        debug_module.h
        debug_rom_defines.h
        remote_bitbang.h
        jtag_dtm.h
        bbtracker.h
        gzstream.h
        ${riscv_gen_hdrs}
)

set(
        riscv_srcs
        processor.cc
        execute.cc
        dts.cc
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
        devices.cc
        rom.cc
        clint.cc
        debug_module.cc
        remote_bitbang.cc
        jtag_dtm.cc
        bbtracker.cc
        gzstream.cc
        ${riscv_gen_srcs}
)


add_library(
        riscv STATIC
        ${riscv_srcs}
        ${riscv_hdrs}
)

target_include_directories(riscv PUBLIC .)
# for statement like "#include <fesvr/htif.h>"
target_include_directories(riscv PUBLIC ${PROJECT_SOURCE_DIR})
# for including the generated code
target_include_directories(riscv PRIVATE ${CMAKE_CURRENT_BINARY_DIR})

target_link_libraries(riscv ac_config)
target_link_libraries(riscv ${CMAKE_DL_LIBS})
target_link_libraries(riscv ZLIB::ZLIB)
target_link_libraries(riscv fesvr)
target_link_libraries(riscv softfloat)
