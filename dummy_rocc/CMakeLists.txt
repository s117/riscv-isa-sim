set(
        dummy_rocc_subproject_deps
        spike_main
        riscv
        softfloat
)

set(
        dummy_rocc_srcs
        dummy_rocc.cc
)

# TODO: complete this target
#add_library(
#        dummy_rocc SHARED
#        ${dummy_rocc_srcs}
#)
#
#target_link_libraries(
#        dummy_rocc ${dummy_rocc_subproject_deps}
#)
