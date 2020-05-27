set(
        spike_main_subproject_deps
        fesvr
        softfloat
        riscv
)


add_executable(spike spike.cc)
target_link_libraries(spike fesvr softfloat riscv)

add_executable(spike-dasm spike-dasm.cc)
target_link_libraries(spike-dasm fesvr softfloat riscv)

add_executable(spike-log-parser spike-log-parser.cc)
target_link_libraries(spike-log-parser fesvr softfloat riscv)

add_executable(xspike xspike.cc)
target_link_libraries(xspike fesvr softfloat riscv)

add_executable(termios-xspike termios-xspike.cc)
target_link_libraries(termios-xspike fesvr softfloat riscv)
