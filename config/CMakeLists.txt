set(
        ac_configure_gen_file_list
        config.h
        config.log
        config.status
        dummy_rocc.mk
        hwacha.mk
        riscv.mk
        softfloat.mk
        spike_main.mk
        Makefile
)

set(
        ac_configure_proj_def
        ../configure
        ../configure.ac
        ../Makefile.in
        ../config.h.in
        ../riscv/riscv.ac
        ../riscv/riscv.mk.in
        ../hwacha/hwacha.ac
        ../hwacha/hwacha.mk.in
        ../dummy_rocc/dummy_rocc.ac
        ../dummy_rocc/dummy_rocc.mk.in
        ../softfloat/softfloat.ac
        ../softfloat/softfloat.mk.in
        ../spike_main/spike_main.ac
        ../spike_main/spike_main.mk.in
)

get_target_property(lib_fesvr_path fesvr-lib-dir FESVR_LIB_DIR)

set(
        ISA_SIM_AC_CONFIGURE_ARGS
        --prefix=${AC_CONFIGURE_PREFIX}
        --with-fesvr=${lib_fesvr_path}
        --enable-simpoint
        --enable-dbg-trace
)

add_custom_command(
        OUTPUT ${ac_configure_gen_file_list}
        COMMAND ${PROJECT_SOURCE_DIR}/configure
        ARGS ${ISA_SIM_AC_CONFIGURE_ARGS}
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

add_dependencies(ac_config fesvr-lib-dir)
