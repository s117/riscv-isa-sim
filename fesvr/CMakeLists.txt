set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

set(
        fesvr_hdrs
        elf.h
        elfloader.h
        htif.h
        dtm.h
        memif.h
        syscall.h
        context.h
        htif_pthread.h
        htif_hexwriter.h
        option_parser.h
        term.h
        device.h
        rfb.h
        tsi.h
)

set(
        fesvr_srcs
        elfloader.cc
        htif.cc
        memif.cc
        dtm.cc
        syscall.cc
        device.cc
        rfb.cc
        context.cc
        htif_pthread.cc
        htif_hexwriter.cc
        dummy.cc
        option_parser.cc
        term.cc
        tsi.cc
)

set(fesvr_CFLAGS "-fPIC")

add_library(
        fesvr STATIC
        ${fesvr_srcs}
        ${fesvr_hdrs}
)

target_compile_options(fesvr PUBLIC "-fPIC")

target_link_libraries(fesvr Threads::Threads)

target_include_directories(
        fesvr PUBLIC
        .
)

target_include_directories(
        fesvr PRIVATE
        ../riscv
)

add_executable(
        elf2hex elf2hex.cc
)

target_link_libraries(
        elf2hex fesvr
)
