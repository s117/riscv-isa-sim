cmake_minimum_required(VERSION 3.23)

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

if (ENABLE_SIMPOINT)
    set(
            ISA_SIM_AC_CONFIGURE_ARGS
            ${ISA_SIM_AC_CONFIGURE_ARGS}
            --enable-simpoint
    )
    message("SimPoint BBV collecting support is enabled.")
endif ()

if (ENABLE_DBG_TRACE)
    set(
            ISA_SIM_AC_CONFIGURE_ARGS
            ${ISA_SIM_AC_CONFIGURE_ARGS}
            --enable-dbg-trace
    )
    message("Instruction tracing support is enabled.")
endif ()

file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/ac_output_dir)

add_custom_command(
        OUTPUT ${ac_configure_gen_file_list}
        COMMAND ${PROJECT_SOURCE_DIR}/configure
        ARGS ${ISA_SIM_AC_CONFIGURE_ARGS}
        WORKING_DIRECTORY ac_output_dir
        DEPENDS ${ac_configure_proj_def}
)

add_library(ac_config INTERFACE ${CMAKE_CURRENT_BINARY_DIR}/ac_output_dir/config.h)
target_compile_definitions(ac_config INTERFACE PREFIX="${PREFIX}")
target_sources(ac_config INTERFACE
        FILE_SET HEADERS
        BASE_DIRS ${CMAKE_CURRENT_BINARY_DIR}/ac_output_dir
        FILES ${CMAKE_CURRENT_BINARY_DIR}/ac_output_dir/config.h
)

add_dependencies(ac_config fesvr-lib-dir)
