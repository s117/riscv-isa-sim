include(ExternalProject)

#set(FESVR_DIR ${FESVR_DIR})
set(FESVR_BUILD_DIR ${CMAKE_CURRENT_BINARY_DIR}/ac_build)
set(FESVR_INSTALL_PREFIX ${AC_CONFIGURE_PREFIX})
set(FESVR_SHARED_LIB ${FESVR_INSTALL_PREFIX}/lib/libfesvr.so)
set(FESVR_INCLUDES ${FESVR_INSTALL_PREFIX}/include)

ExternalProject_Add(
        riscv-fesvr
        SOURCE_DIR ${FESVR_DIR}
        INSTALL_COMMAND ""
        CMAKE_ARGS
            -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}
            -DAC_CONFIGURE_PREFIX=${AC_CONFIGURE_PREFIX}
)

ExternalProject_Get_property(riscv-fesvr BINARY_DIR)
add_library(fesvr STATIC IMPORTED GLOBAL)
set_target_properties(fesvr PROPERTIES IMPORTED_LOCATION ${BINARY_DIR}/libfesvr-static.a)
set_target_properties(fesvr PROPERTIES INTERFACE_INCLUDE_DIRECTORIES ${FESVR_DIR})
