set(
        ac_configure_gen_file_list
        ac_output_dir/config.h
        ac_output_dir/config.log
        ac_output_dir/config.status
        ac_output_dir/dummy_rocc.mk
        ac_output_dir/hwacha.mk
        ac_output_dir/riscv.mk
        ac_output_dir/softfloat.mk
        ac_output_dir/spike_main.mk
        ac_output_dir/Makefile
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
        --prefix=${PREFIX}
        --with-fesvr=${lib_fesvr_path}
)

if(DEFINED ENABLE_SIMPOINT)
        set(
                ISA_SIM_AC_CONFIGURE_ARGS
                ${ISA_SIM_AC_CONFIGURE_ARGS}
                --enable-simpoint
        )
        message("SimPoint BBV collecting support is enabled.")
endif()

if (DEFINED ENABLE_DBG_TRACE)
        set(
                ISA_SIM_AC_CONFIGURE_ARGS
                ${ISA_SIM_AC_CONFIGURE_ARGS}
                --enable-dbg-trace
        )
        message("Instruction tracing support is enabled.")
endif()

file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/ac_output_dir)

add_custom_command(
        OUTPUT ${ac_configure_gen_file_list}
        COMMAND ${PROJECT_SOURCE_DIR}/configure
        ARGS ${ISA_SIM_AC_CONFIGURE_ARGS}
        WORKING_DIRECTORY ac_output_dir
        DEPENDS ${ac_configure_proj_def}
)

add_custom_command(
        OUTPUT dummy.cc
        COMMAND echo "#include \"config.h\"" > dummy.cc
        VERBATIM
)

add_library(
        ac_config STATIC
        ac_output_dir/config.h
        dummy.cc
)

target_include_directories(ac_config PUBLIC ${CMAKE_CURRENT_BINARY_DIR}/ac_output_dir)

target_compile_definitions(
        ac_config
        PUBLIC PREFIX="${PREFIX}"
)

add_dependencies(ac_config fesvr-lib-dir)
