set(
        spike_main_subproject_deps
        ac_config
        fesvr-static
        softfloat
        riscv
)

# Build information (git version, compiler, date...) of spike, printed by spike --version. An ALL custom target runs on
# every build; the script rewrites spike_build_info.cc only when its content changed.
string(TOUPPER "${CMAKE_BUILD_TYPE}" spike_build_type_upper)
add_custom_target(
        spike-build-info ALL
        BYPRODUCTS ${CMAKE_CURRENT_BINARY_DIR}/spike_build_info.cc
        COMMAND sh ${PROJECT_SOURCE_DIR}/scripts/stamp-build-info.sh
                ${CMAKE_CURRENT_BINARY_DIR}/spike_build_info.cc spike "spike" ${PROJECT_SOURCE_DIR}
                ${CMAKE_CXX_COMPILER}
                "${CMAKE_BUILD_TYPE} ${CMAKE_CXX_FLAGS} ${CMAKE_CXX_FLAGS_${spike_build_type_upper}}"
                ${CMAKE_SYSTEM}
        COMMENT "Gathering the build information of spike"
        VERBATIM
)

add_executable(spike spike.cc ${CMAKE_CURRENT_BINARY_DIR}/spike_build_info.cc)
add_dependencies(spike spike-build-info)
target_link_libraries(spike ${spike_main_subproject_deps})
set_target_properties(spike PROPERTIES ENABLE_EXPORTS ON)

add_executable(spike-dasm spike-dasm.cc)
target_link_libraries(spike-dasm ${spike_main_subproject_deps})

add_executable(xspike xspike.cc)
target_link_libraries(xspike ${spike_main_subproject_deps})

add_executable(termios-xspike termios-xspike.cc)
target_link_libraries(termios-xspike ${spike_main_subproject_deps})
