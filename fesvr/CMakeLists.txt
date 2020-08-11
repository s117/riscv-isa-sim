include(ExternalProject)

#set(FESVR_DIR ${FESVR_DIR})
set(FESVR_BUILD_DIR ${CMAKE_CURRENT_BINARY_DIR}/ac_build)
set(FESVR_INSTALL_PREFIX ${AC_CONFIGURE_PREFIX})
set(FESVR_SHARED_LIB ${FESVR_INSTALL_PREFIX}/lib/libfesvr.so)
set(FESVR_INCLUDES ${FESVR_INSTALL_PREFIX}/include)

ExternalProject_Add(
        riscv-fesvr
        SOURCE_DIR ${FESVR_DIR}
        BINARY_DIR ${FESVR_BUILD_DIR}
        CONFIGURE_COMMAND ${FESVR_DIR}/configure --prefix=${FESVR_INSTALL_PREFIX}
        BUILD_COMMAND make
        INSTALL_COMMAND make install
)

add_library(fesvr SHARED IMPORTED GLOBAL)
set_target_properties(fesvr PROPERTIES IMPORTED_LOCATION ${FESVR_SHARED_LIB})
set_target_properties(fesvr PROPERTIES INTERFACE_INCLUDE_DIRECTORIES ${FESVR_INCLUDES})
