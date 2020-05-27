set(
        ac_configure_gen_file_list
        config.h
        config.log
        config.status
        dummy_rocc.mk
        fesvr.mk
        Makefile
        riscv-dummy_rocc.pc
        riscv-fesvr.pc
        riscv.mk
        riscv-riscv.pc
        riscv-softfloat.pc
        riscv-spike_main.pc
        riscv-spike.pc
        softfloat.mk
        spike_main.mk
)

set(
        ac_configure_proj_def
        ../configure
        ../Makefile.in
        ../config.h.in
        ../riscv/riscv.ac
        ../riscv/riscv.mk.in
        ../fesvr/fesvr.ac
        ../fesvr/fesvr.mk.in
        ../softfloat/softfloat.ac
        ../softfloat/softfloat.mk.in
        ../dummy_rocc/dummy_rocc.cc
        ../dummy_rocc/dummy_rocc.mk.in
        ../spike_main/spike_main.ac
        ../spike_main/spike_main.mk.in
)

add_custom_command(
        OUTPUT ${ac_configure_gen_file_list}
        COMMAND ${PROJECT_SOURCE_DIR}/configure
        ARGS ${AC_CONFIGURE_ARGS}
        DEPENDS ${ac_configure_proj_def}
)

add_custom_command(
        OUTPUT dummy.cc
        COMMAND echo "#include \"config.h\"" > dummy.cc
        VERBATIM
)

add_library(
        ac_config STATIC
        config.h
        dummy.cc
)

target_include_directories(ac_config PUBLIC ${CMAKE_CURRENT_BINARY_DIR})

target_compile_definitions(
        ac_config
        PUBLIC PREFIX="${AC_CONFIGURE_PREFIX}"
)