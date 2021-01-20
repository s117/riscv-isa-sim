# Obtaining a list of instructions registered in encoding.h (riscv_insn_list)
execute_process(
        COMMAND ${CMAKE_COMMAND} -E env bash -c "\
            grep ^DECLARE_INSN ${CMAKE_CURRENT_SOURCE_DIR}/encoding.h | \
            sed 's/DECLARE_INSN(\\(.*\\),.*,.*)/\\1/' | \
            sort \
        "
        OUTPUT_VARIABLE riscv_insn_list_raw
        OUTPUT_STRIP_TRAILING_WHITESPACE
)
string(REPLACE "\n" ";" riscv_insn_list ${riscv_insn_list_raw})

# Print the registered instructions
string(REPLACE ";" " " space_sep_insn_list ${riscv_insn_list})
message("Instructions registered in encoding.h: ${space_sep_insn_list}")


# Generate .cc for each registered instruction
macro(riscv_insn_srcs_generator outfiles)
    foreach (i_basename ${ARGN})
        set(i_out ${CMAKE_CURRENT_BINARY_DIR}/${i_basename}.cc)
        add_custom_command(
                DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/encoding.h ${CMAKE_CURRENT_SOURCE_DIR}/insn_template.cc
                OUTPUT ${i_out}
                VERBATIM
                COMMAND ${CMAKE_COMMAND} -E env bash -c "\
                    sed 's/NAME/${i_basename}/' ${CMAKE_CURRENT_SOURCE_DIR}/insn_template.cc | \
                    sed \"s/OPCODE/$( \
                        grep '^DECLARE_INSN.*\\<${i_basename}\\>' ${CMAKE_CURRENT_SOURCE_DIR}/encoding.h | \
                        sed 's/DECLARE_INSN(.*,\\(.*\\),.*)/\\1/' \
                    )/\" >> ${i_out} \
                "
        )
        set(${outfiles} ${${outfiles}} ${i_out})
    endforeach (i_basename)
endmacro(riscv_insn_srcs_generator)
riscv_insn_srcs_generator(riscv_gen_insn_src_list ${riscv_insn_list})

# Generate icache.h using script gen_icache
set(riscv_gen_icache_h ${CMAKE_CURRENT_BINARY_DIR}/icache.h)
add_custom_command(
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/gen_icache ${CMAKE_CURRENT_SOURCE_DIR}/mmu.h
        OUTPUT ${riscv_gen_icache_h}
        VERBATIM
        COMMAND ${CMAKE_COMMAND} -E env bash -c "\
            ${CMAKE_CURRENT_SOURCE_DIR}/gen_icache `grep 'ICACHE_ENTRIES =' ${CMAKE_CURRENT_SOURCE_DIR}/mmu.h | \
            sed 's/.* = \\(.*\\);/\\1/'` \
            >> ${riscv_gen_icache_h} \
        "
)

# Passing the manifest of generated sources with lists riscv_gen_srcs & riscv_gen_hdrs
set(
        riscv_gen_srcs
        ${riscv_gen_insn_src_list}
)

set(
        riscv_gen_hdrs
        ${riscv_gen_icache_h}
)
