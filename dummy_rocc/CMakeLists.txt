set(
        dummy_rocc_subproject_deps
        ac_config
        riscv
        softfloat
)

set(
        dummy_rocc_srcs
        dummy_rocc.cc
)

add_library(
        dummy_rocc MODULE
        ${dummy_rocc_srcs}
)

target_link_libraries(
        dummy_rocc ${dummy_rocc_subproject_deps}
)
