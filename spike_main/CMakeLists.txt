set(
        spike_main_subproject_deps
        ac_config
        fesvr-static
        softfloat
        riscv
)

add_executable(spike spike.cc)
target_link_libraries(spike ${spike_main_subproject_deps})

add_executable(spike-dasm spike-dasm.cc)
target_link_libraries(spike-dasm ${spike_main_subproject_deps})

add_executable(xspike xspike.cc)
target_link_libraries(xspike ${spike_main_subproject_deps})

add_executable(termios-xspike termios-xspike.cc)
target_link_libraries(termios-xspike ${spike_main_subproject_deps})
